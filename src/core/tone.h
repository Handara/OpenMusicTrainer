#pragma once

// A small amp for hearing an instrument through the game: drive (saturation, from clean to warm to fuzzy), tone (a
// low-pass, from dark to bright), volume. Also removes any DC offset the interface sends. Pure, sample by sample, no
// allocation: it runs on the audio thread.

struct ToneSettings {
    float volume = 0.8f; // 0..1
    float drive = 0.0f;  // 0 clean .. 1 heavily driven
    float tone = 0.7f;   // 0 dark .. 1 bright
};

struct ToneState {
    float lowPass = 0.0f;
    float dcIn = 0.0f, dcOut = 0.0f; // the DC blocker's last input and output
};

void processTone(ToneState& state, const ToneSettings& settings, float* samples, int count, int sampleRate);
