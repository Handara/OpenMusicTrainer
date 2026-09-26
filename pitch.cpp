#include "pitch.h"

#include <algorithm>
#include <cmath>

// A lag counts as a period candidate once its normalized difference drops below this (the paper suggests 0.1-0.15)
const float YIN_THRESHOLD = 0.15f;

void initPitchDetector(PitchDetector& detector, int sampleRate, float minFrequency, float maxFrequency){
    detector.sampleRate = sampleRate;
    detector.minLag = (int)(sampleRate / maxFrequency);
    detector.maxLag = (int)std::ceil(sampleRate / minFrequency);
    detector.difference.assign(detector.maxLag + 1, 0.0f);
}

int pitchWindowSize(const PitchDetector& detector){
    return 2 * detector.maxLag;
}

// The idea: a periodic signal lines up with itself when shifted by exactly one period.
// So for each candidate shift ("lag"), measure how different the signal is from its shifted copy,
// and the first lag where the difference nearly vanishes is the period.
PitchResult detectPitch(PitchDetector& detector, const float* samples, int count){
    const PitchResult noPitch = {0.0f, 0.0f};
    const int maxLag = detector.maxLag;
    const int window = count - maxLag; // every lag compares the same number of samples
    if (window <= 0) return noPitch;
    float* d = detector.difference.data();

    // Step 1: difference function. d[lag] = sum of squared differences between the signal and itself shifted by lag
    for (int lag = 1; lag <= maxLag; lag++){
        float sum = 0.0f;
        for (int i = 0; i < window; i++){
            float delta = samples[i] - samples[i + lag];
            sum += delta * delta;
        }
        d[lag] = sum;
    }

    // Step 2: normalize each value by the average of all smaller lags. Without this, tiny lags always
    // look best (a signal barely changes over 1-2 samples). After it, values near 0 mean "periodic here".
    d[0] = 1.0f;
    float runningSum = 0.0f;
    for (int lag = 1; lag <= maxLag; lag++){
        runningSum += d[lag];
        d[lag] = runningSum > 0.0f ? d[lag] * lag / runningSum : 1.0f;
    }

    // Step 3: the first dip below the threshold, followed down to its lowest point. Taking the first one
    // (not the global minimum) avoids octave errors: two periods also line up, but come later.
    int bestLag = -1;
    for (int lag = std::max(detector.minLag, 2); lag < maxLag; lag++){
        if (d[lag] < YIN_THRESHOLD){
            while (lag + 1 < maxLag && d[lag + 1] < d[lag]) lag++;
            bestLag = lag;
            break;
        }
    }
    if (bestLag < 0) return noPitch;

    // Step 4: the true period usually falls between two samples. Fit a parabola through the dip and its
    // neighbours and take the parabola's lowest point, for sub-sample precision (important for bass notes).
    float before = d[bestLag - 1], at = d[bestLag], after = d[bestLag + 1];
    float curvature = before - 2.0f * at + after;
    float shift = curvature != 0.0f ? 0.5f * (before - after) / curvature : 0.0f;

    return { detector.sampleRate / (bestLag + shift), 1.0f - at };
}
