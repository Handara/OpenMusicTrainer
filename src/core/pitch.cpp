#include "core/pitch.h"

#include <algorithm>
#include <cmath>

// A lag counts as a period candidate once its normalized difference drops below this (the paper suggests 0.1-0.15)
const float YIN_THRESHOLD = 0.15f;

const double PI_D = 3.14159265358979323846;

static int powerOfTwoAtLeast(int n){
    int size = 1;
    while (size < n) size <<= 1;
    return size;
}

void initPitchDetector(PitchDetector& detector, int sampleRate, float minFrequency, float maxFrequency){
    detector.sampleRate = sampleRate;
    detector.minLag = (int)(sampleRate / maxFrequency);
    detector.maxLag = (int)std::ceil(sampleRate / minFrequency);
    detector.difference.assign(detector.maxLag + 1, 0.0f);
    detector.fftSize = powerOfTwoAtLeast(pitchWindowSize(detector));
    detector.spectrum.assign(detector.fftSize, 0.0);
    detector.twiddles.resize(detector.fftSize / 2);
    for (int k = 0; k < detector.fftSize / 2; k++) detector.twiddles[k] = std::polar(1.0, -2.0 * PI_D * k / detector.fftSize);
    detector.energy.assign(pitchWindowSize(detector) + 1, 0.0);
}

// In place, a power of two long, at most the detector's size: its twiddles serve every smaller size by stride
static void fft(PitchDetector& detector, std::complex<double>* data, int n){
    for (int i = 1, j = 0; i < n; i++){ // bit-reversed order
        int bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(data[i], data[j]);
    }
    for (int length = 2; length <= n; length <<= 1){
        const int half = length / 2, stride = detector.fftSize / length;
        for (int start = 0; start < n; start += length){
            for (int k = 0; k < half; k++){
                std::complex<double> u = data[start + k], v = data[start + k + half] * detector.twiddles[k * stride];
                data[start + k] = u + v;
                data[start + k + half] = u - v;
            }
        }
    }
}

// YIN's difference function, d[lag] = sum over the window of (x[i] - x[i + lag])^2, for every lag at once. It splits
// into the window's energy, the shifted window's energy (running sums) and their cross-correlation, which one FFT
// finds for every lag: O(n log n) where comparing each lag directly is O(n^2), 15 times less work for a bass.
static void differenceFunction(PitchDetector& detector, const float* samples, int count, int maxLag, float* d){
    const int window = count - maxLag, n = powerOfTwoAtLeast(count);
    std::complex<double>* z = detector.spectrum.data();
    // Two real signals in one complex FFT: the window (zero-padded) as the real part, all the samples as the imaginary
    for (int i = 0; i < n; i++) z[i] = { i < window ? samples[i] : 0.0, i < count ? samples[i] : 0.0 };
    fft(detector, z, n);
    // Their spectra apart (A, the window's; B, all the samples'), then the correlation's spectrum, conj(A) * B, in
    // place. Bins k and n - k are worked out together: each needs the other, and a real signal's correlation has
    // conjugate spectra there.
    for (int k = 0; k <= n / 2; k++){
        int m = (n - k) & (n - 1);
        std::complex<double> zk = z[k], zm = std::conj(z[m]);
        std::complex<double> a = (zk + zm) * 0.5, b = (zk - zm) * std::complex<double>(0.0, -0.5);
        std::complex<double> correlation = std::conj(a) * b;
        z[k] = correlation;
        z[m] = std::conj(correlation);
    }
    // Back: the inverse FFT, as the forward one on the conjugate
    for (int i = 0; i < n; i++) z[i] = std::conj(z[i]);
    fft(detector, z, n);
    double* energy = detector.energy.data();
    energy[0] = 0.0;
    for (int i = 0; i < count; i++) energy[i + 1] = energy[i] + (double)samples[i] * samples[i];
    const double windowEnergy = energy[window];
    for (int lag = 1; lag <= maxLag; lag++){
        double correlation = z[lag].real() / n; // the conjugate of a real value is itself
        double shifted = energy[lag + window] - energy[lag];
        d[lag] = (float)std::max(0.0, windowEnergy + shifted - 2.0 * correlation);
    }
}

int pitchWindowSize(const PitchDetector& detector){
    return 2 * detector.maxLag;
}

// The idea: a periodic signal lines up with itself when shifted by exactly one period.
// So for each candidate shift ("lag"), measure how different the signal is from its shifted copy,
// and the first lag where the difference nearly vanishes is the period.
PitchResult detectPitch(PitchDetector& detector, const float* samples, int count){
    return detectPitch(detector, samples, count, detector.maxLag);
}

PitchResult detectPitch(PitchDetector& detector, const float* samples, int count, int maxLag){
    const PitchResult noPitch = {0.0f, 0.0f};
    maxLag = std::min(maxLag, detector.maxLag);
    const int window = count - maxLag; // every lag compares the same number of samples
    if (window <= 0 || maxLag < std::max(detector.minLag, 2) + 2) return noPitch;
    float* d = detector.difference.data();

    // Step 1: difference function. d[lag] = sum of squared differences between the signal and itself shifted by lag
    differenceFunction(detector, samples, count, maxLag, d);

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
