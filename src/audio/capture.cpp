#include "audio/capture.h"

#include "NAM/get_dsp.h"

#include <algorithm>
#include <exception>
#include <filesystem>
#include <map>
#include <memory>
#include <vector>

namespace {

const int MAX_BLOCK = 512;      // NAM's buffers are made for this many samples at a time
const int PIECE = 256;          // the device's samples taken at once, when the rates differ
const int MAX_RATIO = 8;        // the model's rate at most this many times the device's (48 kHz from 8 kHz)
const double DEFAULT_RATE = 48000.0; // a capture too old to say its rate was made at this one, as nearly all are

// A stream from one rate to another, by cubic interpolation between the samples around each new one: two samples late
struct StreamResampler {
    double step = 1.0;     // input samples per output sample
    double position = 0.0; // where the next output falls, between the middle two of the last four inputs
    float history[4] = {};
    void setRates(int from, int to){ step = (double)from / to; }
    // One input sample; the output samples now due go to `out` (at most `room`): how many
    int push(float x, float* out, int room){
        history[0] = history[1];
        history[1] = history[2];
        history[2] = history[3];
        history[3] = x;
        int made = 0;
        while (position < 1.0 && made < room){
            const float t = (float)position, a = history[0], b = history[1], c = history[2], d = history[3];
            out[made++] = b + 0.5f * t * (c - a + t * (2.0f * a - 5.0f * b + 4.0f * c - d + t * (3.0f * (b - c) + d - a)));
            position += step;
        }
        position -= 1.0;
        return made;
    }
};

// A capture's model as the audio thread runs it: straight through at the rate it was trained at, else between two
// resamplers (the device's rate to the model's and back), all its buffers made beforehand
class NamModel : public CaptureModel {
public:
    NamModel(std::unique_ptr<nam::DSP> model, int rate)
        : dsp(std::move(model)), modelRate(rate),
          modelIn((size_t)(PIECE * MAX_RATIO + 8)), modelOut((size_t)(PIECE * MAX_RATIO + 8)), pending((size_t)(PIECE * 2 + 16)){}

    void process(float* samples, int count, int sampleRate) override {
        if (sampleRate == modelRate || sampleRate <= 0){
            for (int done = 0; done < count;){
                const int piece = std::min(MAX_BLOCK, count - done);
                run(samples + done, samples + done, piece);
                done += piece;
            }
            return;
        }
        if (sampleRate != deviceRate){
            deviceRate = sampleRate;
            toModel = StreamResampler{};
            fromModel = StreamResampler{};
            toModel.setRates(deviceRate, modelRate);
            fromModel.setRates(modelRate, deviceRate);
            std::fill(pending.begin(), pending.end(), 0.0f);
            pendingCount = 4; // a few samples ahead, so a block never comes up short
        }
        for (int done = 0; done < count;){
            const int piece = std::min(PIECE, count - done);
            // The device's samples at the model's rate, through the model, and back
            int made = 0;
            for (int i = 0; i < piece; i++) made += toModel.push(samples[done + i], modelIn.data() + made, (int)modelIn.size() - made);
            for (int at = 0; at < made;){
                const int part = std::min(MAX_BLOCK, made - at);
                run(modelIn.data() + at, modelOut.data() + at, part);
                at += part;
            }
            for (int i = 0; i < made; i++) pendingCount += fromModel.push(modelOut[(size_t)i], pending.data() + pendingCount, (int)pending.size() - pendingCount);
            // Out as many as went in; what's over waits for the next block
            const int out = std::min(piece, pendingCount);
            std::copy(pending.begin(), pending.begin() + out, samples + done);
            std::fill(samples + done + out, samples + done + piece, 0.0f);
            std::copy(pending.begin() + out, pending.begin() + pendingCount, pending.begin());
            pendingCount -= out;
            done += piece;
        }
    }

private:
    void run(float* in, float* out, int count){
        float* inputs[1] = { in };
        float* outputs[1] = { scratch };
        dsp->process(inputs, outputs, count);
        std::copy(scratch, scratch + count, out);
    }

    std::unique_ptr<nam::DSP> dsp;
    int modelRate;
    int deviceRate = 0;
    StreamResampler toModel, fromModel;
    std::vector<float> modelIn, modelOut, pending;
    int pendingCount = 0;
    float scratch[MAX_BLOCK] = {};
};

struct Loaded {
    std::unique_ptr<ToneAsset> asset;
    std::string error;
};

} // namespace

const ToneAsset* captureFromFile(const std::string& path, int slot, std::string& error){
    static std::map<std::string, Loaded> loaded;
    const std::string key = path + "\n" + std::to_string(slot);
    auto found = loaded.find(key);
    if (found != loaded.end()){
        error = found->second.error;
        return found->second.asset.get();
    }
    Loaded& entry = loaded[key];
    const std::string name = std::filesystem::u8path(path).filename().u8string();
    try {
        auto asset = std::make_unique<ToneAsset>();
        for (int runner = 0; runner < TONE_RUNNERS; runner++){
            std::unique_ptr<nam::DSP> dsp = nam::get_dsp(std::filesystem::u8path(path));
            if (!dsp || dsp->NumInputChannels() != 1){
                entry.error = name + " isn't a capture lahn can play (one input, one output)";
                error = entry.error;
                return nullptr;
            }
            const double rate = dsp->GetExpectedSampleRate() > 0.0 ? dsp->GetExpectedSampleRate() : DEFAULT_RATE;
            dsp->ResetAndPrewarm(rate, MAX_BLOCK); // settled here, not on the audio thread
            if (runner == 0 && dsp->HasLoudness()) asset->loudnessDb = (float)dsp->GetLoudness();
            asset->models[runner] = std::make_unique<NamModel>(std::move(dsp), (int)rate);
        }
        entry.asset = std::move(asset);
    } catch (const std::exception& e){
        entry.error = name + " isn't a capture lahn can play: " + e.what();
    }
    error = entry.error;
    return entry.asset.get();
}

void attachCaptures(ToneParameters& parameters, const std::string& folder){
    for (int i = 0; i < parameters.count; i++){
        const Effect& effect = parameters.effects[i];
        if (effect.type != EffectType::Capture) continue;
        parameters.assets[i] = nullptr;
        if (!effect.file[0] || folder.empty()) continue;
        std::string error;
        parameters.assets[i] = captureFromFile((std::filesystem::u8path(folder) / std::filesystem::u8path(effect.file)).u8string(), i, error);
    }
}
