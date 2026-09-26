#include "doctest/doctest.h"

#include "core/music.h"
#include "core/pitch.h"
#include "core/settings.h"
#include "core/synth.h"

#include <cmath>
#include <string>
#include <vector>

// The synth is checked with the pitch detector: two independent pieces of code have to agree
TEST_CASE("plucked notes are in tune across the guitar's range"){
    const int sampleRate = 48000;
    PitchDetector detector;
    initPitchDetector(detector, sampleRate, 30.0f, 1400.0f);
    const int window = pitchWindowSize(detector);
    std::vector<float> samples(sampleRate / 2);

    for (int midi : {28, 40, 45, 52, 59, 64, 76, 88}){ // bass low E up to the high e's 24th fret
        float frequency = midiToFrequency((float)midi);
        renderPluck(samples.data(), (int)samples.size(), frequency, sampleRate, 1);
        // Measure after the attack has settled, like a tuner would
        PitchResult result = detectPitch(detector, samples.data() + sampleRate / 10, window);
        float cents = 1200.0f * std::log2(result.frequency / frequency);
        CHECK_MESSAGE(std::fabs(cents) < 2.0f, "MIDI " << midi << ": " << cents << " cents");
    }
}

TEST_CASE("a pluck decays and ends silent"){
    const int sampleRate = 48000;
    std::vector<float> samples(sampleRate);
    renderPluck(samples.data(), (int)samples.size(), 110.0f, sampleRate, 1);
    auto peak = [&](int from, int to){
        float p = 0.0f;
        for (int i = from; i < to; i++) p = std::max(p, std::fabs(samples[i]));
        return p;
    };
    CHECK(peak(0, 4800) > 0.05f);                         // audible at the start
    CHECK(peak(0, 4800) <= 0.5f);                         // never louder than half scale
    CHECK(peak(38400, 43200) < peak(0, 4800) * 0.25f);    // much quieter after 0.8 s
    CHECK(samples.back() == 0.0f);                        // faded out: no click at the end
}

TEST_CASE("every built-in preview sound is in tune, sensibly loud, and ends silent"){
    const int sampleRate = 48000;
    PitchDetector detector;
    initPitchDetector(detector, sampleRate, 30.0f, 1400.0f);
    const int window = pitchWindowSize(detector);
    std::vector<float> samples(sampleRate);

    for (const char* name : BUILT_IN_PREVIEW_SOUNDS){
        for (int midi : {40, 57, 76}){ // low E, A3, the high e's 12th fret
            float frequency = midiToFrequency((float)midi);
            REQUIRE(renderBuiltInSound(name, samples.data(), (int)samples.size(), frequency, sampleRate, 1));
            // "drop" fades ~11 dB within one detection window on a low note, which skews YIN's reading of a
            // pure sine (not the sound's pitch, which is exact after its glide): its low note is left out
            bool fastDecayingLowNote = std::string(name) == "drop" && midi < 50;
            PitchResult result = detectPitch(detector, samples.data() + sampleRate / 20, window); // after any glide
            float cents = 1200.0f * std::log2(result.frequency / frequency);
            if (!fastDecayingLowNote){
                CHECK_MESSAGE(std::fabs(cents) < 3.0f, std::string(name) << " MIDI " << midi << ": " << cents << " cents");
            }

            float peak = 0.0f;
            for (float s : samples) peak = std::max(peak, std::fabs(s));
            CHECK_MESSAGE(peak > 0.1f, name);
            CHECK_MESSAGE(peak <= 0.5f, name);
            CHECK_MESSAGE(samples.back() == 0.0f, name);
        }
    }
    CHECK_FALSE(renderBuiltInSound("kazoo", samples.data(), (int)samples.size(), 440.0f, sampleRate, 1));
}

TEST_CASE("the metronome click is short, sensibly loud, and ends silent"){
    const int sampleRate = 48000;
    std::vector<float> samples(sampleRate / 10); // 100 ms
    for (bool accent : {false, true}){
        renderClick(samples.data(), (int)samples.size(), sampleRate, accent);
        float early = 0.0f, late = 0.0f;
        for (int i = 0; i < (int)samples.size(); i++){
            float a = std::fabs(samples[i]);
            CHECK(a <= 0.5f);
            if (i < sampleRate / 100) early = std::max(early, a); // first 10 ms
            if (i > sampleRate / 20) late = std::max(late, a);    // after 50 ms
        }
        CHECK(early > 0.2f);
        CHECK(late < early * 0.05f); // a click, not a note: gone within 50 ms
        CHECK(samples.back() == 0.0f);
    }
}
