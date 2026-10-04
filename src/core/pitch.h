#pragma once

#include <complex>
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
    // The FFT's work (see detectPitch): allocated once, sized for the longest window
    int fftSize = 0;
    std::vector<std::complex<double>> spectrum;
    std::vector<std::complex<double>> twiddles; // e^(-2 pi i k / fftSize), for every FFT size up to it
    std::vector<double> energy;                 // running sums of the samples' squares
};

void initPitchDetector(PitchDetector& detector, int sampleRate, float minFrequency, float maxFrequency);

// How many samples detectPitch should be given: two periods of the lowest frequency
int pitchWindowSize(const PitchDetector& detector);

PitchResult detectPitch(PitchDetector& detector, const float* samples, int count);
// The same, searching periods only up to `maxLag` (at most the detector's): when the lowest note that can come is
// known to be higher, it needs just 2 * maxLag samples, sooner and for far less work (YIN's cost grows with the
// square of the window)
PitchResult detectPitch(PitchDetector& detector, const float* samples, int count, int maxLag);

// The power of one frequency in some samples (Hann-windowed, so a strong neighbour doesn't leak into it): for
// comparing one frequency with another, or one stretch of sound with another as long
double powerAt(const float* samples, int count, double frequency, int sampleRate);
