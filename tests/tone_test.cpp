#include "doctest/doctest.h"

#include "core/tone.h"

#include <cmath>
#include <vector>

const int RATE = 48000;

static std::vector<float> sine(float frequency, float amplitude, int count){
    std::vector<float> out(count);
    for (int i = 0; i < count; i++) out[i] = amplitude * (float)std::sin(2 * 3.14159265 * frequency * i / RATE);
    return out;
}

// The loudest of the second half, once the filters have settled
static float peak(const std::vector<float>& samples){
    float most = 0.0f;
    for (size_t i = samples.size() / 2; i < samples.size(); i++) most = std::max(most, std::fabs(samples[i]));
    return most;
}

TEST_CASE("clean, bright and at full volume, a bass note comes through as it was"){
    std::vector<float> note = sine(55.0f, 0.5f, RATE);
    ToneState state;
    processTone(state, { 1.0f, 0.0f, 1.0f }, note.data(), (int)note.size(), RATE);
    CHECK(peak(note) == doctest::Approx(0.5f).epsilon(0.03));
}

TEST_CASE("the tone knob darkens: highs go, lows stay"){
    std::vector<float> low = sine(100.0f, 0.5f, RATE), high = sine(5000.0f, 0.5f, RATE);
    ToneState a, b;
    processTone(a, { 1.0f, 0.0f, 0.0f }, low.data(), (int)low.size(), RATE);
    processTone(b, { 1.0f, 0.0f, 0.0f }, high.data(), (int)high.size(), RATE);
    CHECK(peak(low) > 0.4f);
    CHECK(peak(high) < 0.05f);
}

TEST_CASE("drive pushes quiet playing up and rounds loud playing off, never past full scale"){
    std::vector<float> quiet = sine(110.0f, 0.05f, RATE), loud = sine(110.0f, 1.0f, RATE);
    ToneState a, b;
    processTone(a, { 1.0f, 0.8f, 1.0f }, quiet.data(), (int)quiet.size(), RATE);
    processTone(b, { 1.0f, 0.8f, 1.0f }, loud.data(), (int)loud.size(), RATE);
    CHECK(peak(quiet) > 0.5f); // a twentieth of full scale comes out over half of it
    CHECK(peak(loud) <= 1.0f);
}

TEST_CASE("volume scales, and a DC offset is taken out"){
    std::vector<float> offset(RATE, 0.3f); // an interface sending a constant 0.3
    ToneState state;
    processTone(state, { 1.0f, 0.0f, 1.0f }, offset.data(), (int)offset.size(), RATE);
    CHECK(std::fabs(offset.back()) < 0.01f);
    std::vector<float> note = sine(55.0f, 0.5f, RATE);
    ToneState half;
    processTone(half, { 0.5f, 0.0f, 1.0f }, note.data(), (int)note.size(), RATE);
    CHECK(peak(note) == doctest::Approx(0.25f).epsilon(0.03));
}
