#pragma once

#include <vector>

// Monophonic pitch detection with the YIN algorithm (de Cheveigné & Kawahara, 2002).
// Pure math on samples: no audio device or graphics involved, so it's reusable (tuner, gameplay, singing).

struct PitchResult {
    float frequency; // Hz, 0 if no clear pitch was found
    float clarity;   // 0..1, how periodic the signal is (1 = perfectly periodic)
};

struct PitchDetector {
    int sampleRate = 0;
    int minLag = 0; // shortest period searched, in samples (= highest frequency)
    int maxLag = 0; // longest period searched, in samples (= lowest frequency)
    std::vector<float> difference; // work buffer, allocated once in initPitchDetector, reused every call
};

void initPitchDetector(PitchDetector& detector, int sampleRate, float minFrequency, float maxFrequency);

// How many samples detectPitch should be given: two periods of the lowest frequency
int pitchWindowSize(const PitchDetector& detector);

PitchResult detectPitch(PitchDetector& detector, const float* samples, int count);
