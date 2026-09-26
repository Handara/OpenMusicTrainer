#include "core/synth.h"

#include "core/settings.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <random>
#include <vector>

const float PLUCK_DECAY_S = 1.5f; // time for a note to fade by 60 dB
const float END_FADE_S = 0.01f;   // short fade at the very end so a sound never stops with a click
const float PEAK_LEVEL = 0.5f;    // every sound peaks around half of full scale, so they can overlap safely
const double TWO_PI = 6.283185307179586;

// Multiplier per sample that makes a sound fade by 60 dB (to 1/1000) over `seconds`
static float decayPerSample(float seconds, int sampleRate){
    return std::pow(0.001f, 1.0f / (seconds * sampleRate));
}

// Linear fade over the last few milliseconds, reaching exactly 0 on the last sample
static void fadeEnd(float* out, int count, int sampleRate){
    int fadeLength = std::min(count - 1, (int)(END_FADE_S * sampleRate));
    for (int i = 0; i < fadeLength; i++) out[count - 1 - i] *= (float)i / fadeLength;
}

void renderPluck(float* out, int count, float frequency, int sampleRate, unsigned seed){
    // The loop must delay the signal by exactly one period. Three parts add up to it:
    // the delay line (whole samples) + the averaging filter (always half a sample) + an allpass filter
    // (the leftover fraction). Without the allpass, high notes would be up to ~20 cents out of tune.
    float period = sampleRate / frequency;
    int lineLength = std::max(2, (int)(period - 0.5f - 0.1f)); // keeps the fraction in 0.1..1.1, where the allpass is accurate
    float fraction = period - 0.5f - lineLength;
    float allpassCoefficient = (1.0f - fraction) / (1.0f + fraction);

    // Loss per trip around the loop, so every pitch takes the same time to fade (high notes loop more often)
    float loopGain = std::pow(10.0f, -3.0f / (PLUCK_DECAY_S * frequency));

    // The pluck itself: noise, slightly low-passed so it sounds like a finger rather than a pick scratch
    std::vector<float> line(lineLength);
    std::minstd_rand rng(seed);
    std::uniform_real_distribution<float> noise(-1.0f, 1.0f);
    float smoothed = 0.0f;
    for (float& sample : line){
        smoothed += 0.5f * (noise(rng) - smoothed);
        sample = smoothed;
    }

    float previous = 0.0f, allpassIn = 0.0f, allpassOut = 0.0f;
    for (int i = 0, position = 0; i < count; i++){
        float current = line[position];
        float averaged = 0.5f * (current + previous) * loopGain;
        previous = current;
        float delayed = allpassCoefficient * averaged + allpassIn - allpassCoefficient * allpassOut;
        allpassIn = averaged;
        allpassOut = delayed;
        line[position] = delayed;
        position = (position + 1) % lineLength;

        out[i] = PEAK_LEVEL * current;
    }
    fadeEnd(out, count, sampleRate);
}

void renderSoftTone(float* out, int count, float frequency, int sampleRate){
    const float attackSamples = 0.008f * sampleRate; // 8 ms: soft, but no audible delay
    const float decay = decayPerSample(1.2f, sampleRate);
    float envelope = 1.0f;
    for (int i = 0; i < count; i++){
        double phase = TWO_PI * frequency * i / sampleRate;
        float tone = (float)(std::sin(phase) + 0.3 * std::sin(2 * phase) + 0.1 * std::sin(3 * phase)) / 1.4f;
        float attack = std::min(1.0f, i / attackSamples);
        out[i] = PEAK_LEVEL * tone * attack * envelope;
        envelope *= decay;
    }
    fadeEnd(out, count, sampleRate);
}

void renderKeys(float* out, int count, float frequency, int sampleRate){
    const float attackSamples = 0.003f * sampleRate;
    const float decay = decayPerSample(1.5f, sampleRate);
    const float brightnessDecay = decayPerSample(0.6f, sampleRate); // the wobble fades faster than the note
    float envelope = 1.0f, modulationIndex = 1.5f;
    for (int i = 0; i < count; i++){
        double phase = TWO_PI * frequency * i / sampleRate;
        float tone = (float)std::sin(phase + modulationIndex * std::sin(phase)); // modulator at the same frequency: harmonic, in tune
        float attack = std::min(1.0f, i / attackSamples);
        out[i] = PEAK_LEVEL * tone * attack * envelope;
        envelope *= decay;
        modulationIndex *= brightnessDecay;
    }
    fadeEnd(out, count, sampleRate);
}

void renderDrop(float* out, int count, float frequency, int sampleRate){
    const float glideSamples = 0.025f * sampleRate; // reaches the note's pitch after 25 ms
    const float decay = decayPerSample(0.35f, sampleRate);
    const float attackSamples = 0.001f * sampleRate;
    float envelope = 1.0f;
    double phase = 0.0;
    for (int i = 0; i < count; i++){
        // Frequency slides up from 60% of the note (a bit under an octave below) to the note, easing in
        float progress = std::min(1.0f, i / glideSamples);
        float glide = 0.6f + 0.4f * (1.0f - (1.0f - progress) * (1.0f - progress));
        phase += TWO_PI * frequency * glide / sampleRate; // accumulate: the frequency changes as it goes
        float attack = std::min(1.0f, i / attackSamples);
        out[i] = PEAK_LEVEL * (float)std::sin(phase) * attack * envelope;
        envelope *= decay;
    }
    fadeEnd(out, count, sampleRate);
}

bool renderBuiltInSound(const char* name, float* out, int count, float frequency, int sampleRate, unsigned seed){
    if (std::strcmp(name, "pluck") == 0) renderPluck(out, count, frequency, sampleRate, seed);
    else if (std::strcmp(name, "soft") == 0) renderSoftTone(out, count, frequency, sampleRate);
    else if (std::strcmp(name, "keys") == 0) renderKeys(out, count, frequency, sampleRate);
    else if (std::strcmp(name, "drop") == 0) renderDrop(out, count, frequency, sampleRate);
    else return false;
    return true;
}
