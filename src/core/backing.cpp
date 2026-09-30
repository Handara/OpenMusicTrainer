#include "core/backing.h"

#include "core/files.h"
#include "core/music.h"
#include "core/synth.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

const double TAIL_S = 2.0;           // after the last note, for it to ring out
const float MIN_NOTE_S = 0.15f;      // the shortest a note sounds
const float MAX_NOTE_S = 6.0f;       // the longest (a long tied note fades out before then anyway)
const float MUTE_S = 0.06f;          // a note ending is muted this quickly, as a hand on the strings does
const float GUITAR_GAIN = 0.32f;
const float BASS_GAIN = 0.5f;
const float CLICK_GAIN = 0.22f;
const float PEAK = 0.9f;             // the loudest the backing gets

std::vector<float> renderBacking(const Chart& chart, int sampleRate){
    const double length = tickToSeconds(chart, chart.endTick) + TAIL_S;
    std::vector<float> out((size_t)(length * sampleRate) + 1, 0.0f);
    std::vector<float> note;
    auto add = [&](double at, const std::vector<float>& sound, int count, float gain){
        long long start = (long long)std::llround(at * sampleRate);
        for (int i = 0; i < count; i++){
            long long k = start + i;
            if (k >= 0 && k < (long long)out.size()) out[(size_t)k] += sound[i] * gain;
        }
    };

    // Every part, each note for as long as it's held, then muted
    const float parts = (float)std::max<size_t>(1, chart.frettedTracks.size());
    unsigned seed = 1;
    for (const FrettedTrack& track : chart.frettedTracks){
        const bool bass = track.type == InstrumentType::Bass;
        for (const FrettedNote& played : track.notes){
            if (played.stringIndex < 0 || played.stringIndex >= (int)track.tuning.size()) continue;
            double at = tickToSeconds(chart, played.tick);
            float held = (float)(tickToSeconds(chart, played.tick + std::max(played.duration, 1)) - at);
            held = std::clamp(held, MIN_NOTE_S, MAX_NOTE_S);
            int count = (int)((held + MUTE_S) * sampleRate);
            note.assign(count, 0.0f);
            float frequency = midiToFrequency((float)(track.tuning[played.stringIndex] + played.fret));
            if (bass) renderBass(note.data(), count, frequency, sampleRate);
            else renderPluck(note.data(), count, frequency, sampleRate, seed++);
            int muteFrom = (int)(held * sampleRate);
            for (int i = muteFrom; i < count; i++) note[i] *= 1.0f - (float)(i - muteFrom) / (count - muteFrom);
            add(at, note, count, (bass ? BASS_GAIN : GUITAR_GAIN) / std::sqrt(parts));
        }
    }

    // A click on every beat, the first of each bar louder: the song's pulse, where the drums would be
    const int clickCount = (int)(0.05 * sampleRate);
    std::vector<float> click(clickCount), accent(clickCount);
    renderClick(click.data(), clickCount, sampleRate, false);
    renderClick(accent.data(), clickCount, sampleRate, true);
    for (int tick = 0, bar = 0; tick < chart.endTick; bar++){
        const TimeSignatureChange& time = timeSignatureAt(chart, tick);
        const int beat = std::max(1, chart.resolution * 4 / std::max(1, time.beatUnit));
        for (int b = 0; b < time.beats && tick < chart.endTick; b++, tick += beat){
            add(tickToSeconds(chart, tick), b == 0 ? accent : click, clickCount, CLICK_GAIN);
        }
    }

    // Never louder than the peak: the parts together can add up
    float loudest = 0.0f;
    for (float sample : out) loudest = std::max(loudest, std::fabs(sample));
    if (loudest > PEAK) for (float& sample : out) sample *= PEAK / loudest;
    return out;
}

static void put16(std::string& data, int value){ data += (char)(value & 0xFF); data += (char)((value >> 8) & 0xFF); }
static void put32(std::string& data, uint32_t value){ for (int i = 0; i < 4; i++) data += (char)((value >> (8 * i)) & 0xFF); }

bool writeWav(const std::string& path, const std::vector<float>& samples, int sampleRate, std::string& error){
    std::string data;
    const uint32_t bytes = (uint32_t)samples.size() * 2;
    data.reserve(44 + bytes);
    data += "RIFF";
    put32(data, 36 + bytes);
    data += "WAVEfmt ";
    put32(data, 16);
    put16(data, 1);                 // PCM
    put16(data, 1);                 // mono
    put32(data, (uint32_t)sampleRate);
    put32(data, (uint32_t)sampleRate * 2);
    put16(data, 2);                 // bytes per frame
    put16(data, 16);                // bits per sample
    data += "data";
    put32(data, bytes);
    for (float sample : samples) put16(data, (int)std::lround(std::clamp(sample, -1.0f, 1.0f) * 32767.0f));
    return writeFileAtomically(path, data, error);
}
