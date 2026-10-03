#include "doctest/doctest.h"

#include "core/timestretch.h"

#include <cmath>
#include <vector>

// A sound through the stretch, fed in blocks as the audio engine would, taken out in blocks of another size
static std::vector<float> stretched(const std::vector<float>& sound, int channels, int rate, float speed){
    TimeStretch stretch;
    initTimeStretch(stretch, channels, rate, speed);
    std::vector<float> out, block(500 * channels);
    size_t fed = 0;
    const size_t frames = sound.size() / channels;
    while (true){
        int wants = timeStretchWants(stretch);
        if (wants > 0 && fed < frames){
            int count = (int)std::min<size_t>(std::min(wants, 1000), frames - fed);
            feedTimeStretch(stretch, sound.data() + fed * channels, count);
            fed += count;
            if (fed == frames) endTimeStretch(stretch);
            continue;
        }
        if (fed == frames) endTimeStretch(stretch);
        int got = takeTimeStretch(stretch, block.data(), 500);
        if (got == 0) break;
        out.insert(out.end(), block.begin(), block.begin() + (size_t)got * channels);
    }
    return out;
}

// The pitch of a steady sound: crossings upward through zero, a second
static double crossingsPerSecond(const std::vector<float>& sound, int channels, int channel, int rate, size_t from, size_t to){
    int count = 0;
    for (size_t i = from + 1; i < to; i++) if (sound[(i - 1) * channels + channel] < 0.0f && sound[i * channels + channel] >= 0.0f) count++;
    return count * (double)rate / (to - from);
}

TEST_CASE("slower, a sound lasts longer and keeps its pitch"){
    const int rate = 44100;
    std::vector<float> tone(rate * 2);
    for (size_t i = 0; i < tone.size(); i++) tone[i] = 0.5f * std::sin(2.0 * 3.14159265358979 * 110.0 * i / rate); // A2
    for (float speed : { 0.5f, 0.7f, 0.85f, 1.0f, 1.25f }){
        CAPTURE(speed);
        std::vector<float> out = stretched(tone, 1, rate, speed);
        CHECK(out.size() == doctest::Approx(tone.size() / speed).epsilon(0.02));
        // Its pitch, away from the ends: still 110 Hz
        CHECK(crossingsPerSecond(out, 1, 0, rate, rate / 4, out.size() - rate / 4) == doctest::Approx(110.0).epsilon(0.01));
        // And as loud all through: the pieces join without dips or clicks
        float lowest = 1.0f, highest = 0.0f;
        for (size_t start = rate / 4; start + rate / 50 < out.size() - rate / 4; start += rate / 50){
            float peak = 0.0f;
            for (size_t i = start; i < start + rate / 50; i++) peak = std::max(peak, std::fabs(out[i]));
            lowest = std::min(lowest, peak);
            highest = std::max(highest, peak);
        }
        CHECK(lowest > 0.45f);
        CHECK(highest < 0.56f);
    }
}

TEST_CASE("each channel is stretched as itself, and in step with the others"){
    const int rate = 22050;
    std::vector<float> stereo(rate * 2 * 2);
    for (size_t i = 0; i < stereo.size() / 2; i++){
        stereo[2 * i] = 0.4f * std::sin(2.0 * 3.14159265358979 * 82.4 * i / rate);      // E2 on the left
        stereo[2 * i + 1] = 0.4f * std::sin(2.0 * 3.14159265358979 * 220.0 * i / rate); // A3 on the right
    }
    std::vector<float> out = stretched(stereo, 2, rate, 0.6f);
    const size_t frames = out.size() / 2;
    CHECK(frames == doctest::Approx(stereo.size() / 2 / 0.6).epsilon(0.02));
    CHECK(crossingsPerSecond(out, 2, 0, rate, rate / 4, frames - rate / 4) == doctest::Approx(82.4).epsilon(0.015));
    CHECK(crossingsPerSecond(out, 2, 1, rate, rate / 4, frames - rate / 4) == doctest::Approx(220.0).epsilon(0.01));
}

TEST_CASE("a note's start comes out where the speed puts it"){
    // Silence, then a note at 1.0 s: at 70% it starts at 1.0 / 0.7 s, give or take the lag the stretch says it has
    const int rate = 44100;
    std::vector<float> sound(rate * 2, 0.0f);
    for (size_t i = rate; i < sound.size(); i++) sound[i] = 0.5f * std::sin(2.0 * 3.14159265358979 * 196.0 * (i - rate) / rate);
    const float speed = 0.7f;
    std::vector<float> out = stretched(sound, 1, rate, speed);
    size_t start = 0;
    while (start < out.size() && std::fabs(out[start]) < 0.05f) start++;
    TimeStretch stretch;
    initTimeStretch(stretch, 1, rate, speed);
    double expected = (rate - timeStretchLag(stretch)) / speed; // the song frame heard at t is about t * speed + lag
    CHECK(std::fabs((double)start - expected) < 0.012 * rate);
}

TEST_CASE("starting again somewhere else drops what was made"){
    TimeStretch stretch;
    initTimeStretch(stretch, 1, 44100, 0.5f);
    std::vector<float> noise(5000, 0.3f);
    feedTimeStretch(stretch, noise.data(), 5000);
    resetTimeStretch(stretch, 88200);
    CHECK(stretch.inputStart == 88200);
    CHECK(stretch.input.empty());
    CHECK(timeStretchWants(stretch) > 0);
    std::vector<float> out(100);
    CHECK(takeTimeStretch(stretch, out.data(), 100) == 0); // nothing until the new place is fed
}
