#include "doctest/doctest.h"

#include "core/ampmodel.h"

#include <cmath>
#include <complex>
#include <string>
#include <vector>

const int RATE = 48000;
const double PI_T = 3.14159265358979323846;

// How much of a frequency a stretch of sound holds (a windowed DFT bin's power)
static double power(const std::vector<float>& x, double frequency){
    std::complex<double> sum = 0.0;
    for (size_t i = 0; i < x.size(); i++){
        const double window = 0.5 - 0.5 * std::cos(2.0 * PI_T * (double)i / (double)(x.size() - 1));
        sum += window * x[i] * std::polar(1.0, -2.0 * PI_T * frequency * (double)i / RATE);
    }
    return std::norm(sum);
}

// A sine through an amp, after it settles
static std::vector<float> throughAmp(int model, float gain, double frequency, float amplitude){
    AmpState amp;
    const float values[6] = { (float)model, gain, 0.5f, 0.5f, 0.5f, 0.5f };
    setUpAmp(amp, values, RATE);
    std::vector<float> out(8192);
    for (int i = 0; i < 4000; i++) processAmp(amp, amplitude * (float)std::sin(2.0 * PI_T * frequency * i / RATE));
    for (size_t i = 0; i < out.size(); i++) out[i] = processAmp(amp, amplitude * (float)std::sin(2.0 * PI_T * frequency * (double)(i + 4000) / RATE));
    return out;
}

TEST_CASE("amps: each tone stack's knobs do what they say, around the amp's own shape"){
    for (int model = 0; model < ampModelCount(); model++){
        const std::string name = ampModelNames()[model];
        CAPTURE(name);
        const float rate = RATE * OVERSAMPLING;
        CHECK(toneStackDb(model, 0.5f, 0.5f, 1.0f, 60.0f, rate) > toneStackDb(model, 0.5f, 0.5f, 0.0f, 60.0f, rate) + 3.0f);
        CHECK(toneStackDb(model, 1.0f, 0.5f, 0.5f, 5000.0f, rate) > toneStackDb(model, 0.0f, 0.5f, 0.5f, 5000.0f, rate) + 3.0f);
        CHECK(toneStackDb(model, 0.5f, 1.0f, 0.5f, 600.0f, rate) > toneStackDb(model, 0.5f, 0.0f, 0.5f, 600.0f, rate) + 3.0f);
        // The passive stack's scoop: at noon, the mids sit under the lows and the highs
        const float mids = toneStackDb(model, 0.5f, 0.5f, 0.5f, 700.0f, rate);
        CHECK(mids < toneStackDb(model, 0.5f, 0.5f, 0.5f, 80.0f, rate));
        CHECK(mids < toneStackDb(model, 0.5f, 0.5f, 0.5f, 6000.0f, rate));
    }
}

TEST_CASE("amps: more gain, more distortion; the harmonics above the device's rate don't fold back down"){
    for (int model = 0; model < ampModelCount(); model++){
        const std::string name = ampModelNames()[model];
        CAPTURE(name);
        // The harmonics against the note, played softly, the gain at its bottom and at its top
        auto distortion = [&](float gain){
            const std::vector<float> out = throughAmp(model, gain, 220.0, 0.05f);
            double harmonics = 0.0;
            for (int h = 2; h <= 8; h++) harmonics += power(out, 220.0 * h);
            return harmonics / power(out, 220.0);
        };
        CHECK(distortion(1.0f) > 2.0 * distortion(0.0f));
        // A high note pushed hard: what isn't one of its harmonics (folded back down, aliased) stays far under them
        const std::vector<float> loud = throughAmp(model, 0.8f, 4100.0, 0.3f);
        double on = 0.0, off = 0.0;
        for (double f = 100.0; f < 23500.0; f += 50.0){
            const double nearest = std::round(f / 4100.0) * 4100.0;
            (std::fabs(f - nearest) < 120.0 && nearest > 0.0 ? on : off) += power(loud, f);
        }
        CHECK(10.0 * std::log10(off / on) < -30.0);
    }
}

TEST_CASE("amps and drives: silence stays silent, loud playing stays finite"){
    for (int model = 0; model < ampModelCount(); model++){
        AmpState amp;
        const float values[6] = { (float)model, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f };
        setUpAmp(amp, values, RATE);
        float quietest = 0.0f, loudest = 0.0f;
        for (int i = 0; i < 4800; i++) quietest = std::max(quietest, std::fabs(processAmp(amp, 0.0f)));
        for (int i = 0; i < 4800; i++){
            const float y = processAmp(amp, (i / 50) % 2 ? 1.0f : -1.0f);
            REQUIRE(std::isfinite(y));
            loudest = std::max(loudest, std::fabs(y));
        }
        CHECK(quietest < 1e-6f);
        CHECK(loudest < 10.0f);
    }
    for (int type = 0; type < (int)DriveType::Count; type++){
        DriveState drive;
        const float values[5] = { 1.0f, (float)type, 1.0f, 1.0f, 1.0f };
        setUpDrive(drive, values, RATE);
        float quietest = 0.0f;
        for (int i = 0; i < 4800; i++) quietest = std::max(quietest, std::fabs(processDrive(drive, 0.0f)));
        CHECK(quietest < 1e-6f);
        for (int i = 0; i < 4800; i++) REQUIRE(std::isfinite(processDrive(drive, (i / 50) % 2 ? 1.0f : -1.0f)));
    }
}
