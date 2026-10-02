#include "core/stemsplit.h"

#include "core/fft.h"

#include <algorithm>
#include <cmath>
#include <complex>

const int N_FFT = 16384;                       // a frame: 0.37 s, long enough to tell a bass's low notes apart
const int HOP = 1024;                          // from one frame to the next
const int CHUNK = HOP * (STEM_FRAMES - 1);     // the samples a chunk's frames cover
const int TRIM = N_FFT / 2;                    // a chunk's ends are thrown away: the transform is unsure there
const int KEPT = CHUNK - 2 * TRIM;             // what's kept of each chunk, and how far apart chunks start
const float PI_F = 3.14159265358979f;

bool splitStem(const std::vector<float>& left, const std::vector<float>& right, const StemModel& model,
               std::vector<float>& stemLeft, std::vector<float>& stemRight, std::atomic<float>* progress,
               const std::atomic<bool>& cancel, std::string& error){
    const size_t length = std::min(left.size(), right.size());
    if (length == 0){
        error = "there's no sound in it";
        return false;
    }
    std::vector<float> window(N_FFT);
    for (int i = 0; i < N_FFT; i++) window[i] = 0.5f - 0.5f * std::cos(2.0f * PI_F * i / N_FFT); // Hann
    const std::vector<std::complex<float>> twiddles = fftTwiddles(N_FFT);
    // The overlapping frames' windows, squared and added up: what the sound is divided by, put back together
    std::vector<float> weight(CHUNK + N_FFT, 0.0f);
    for (int t = 0; t < STEM_FRAMES; t++) for (int i = 0; i < N_FFT; i++) weight[t * HOP + i] += window[i] * window[i];

    const size_t padded = (length / KEPT + 1) * (size_t)KEPT; // whole chunks: the last one's rest is silence
    stemLeft.assign(padded, 0.0f);
    stemRight.assign(padded, 0.0f);
    const std::vector<float>* channels[2] = { &left, &right };
    std::vector<float>* stems[2] = { &stemLeft, &stemRight };
    std::vector<float> input(STEM_TENSOR), output(STEM_TENSOR), chunk(CHUNK), sound(CHUNK + N_FFT);
    std::vector<std::complex<float>> frame(N_FFT);

    for (size_t at = 0; at < padded; at += KEPT){
        if (cancel) return false;
        // The chunk starts TRIM before what's kept of it; then each frame of each channel, into the model's input
        for (int c = 0; c < 2; c++){
            for (int i = 0; i < CHUNK; i++){
                long long sample = (long long)at - TRIM + i;
                chunk[i] = sample >= 0 && sample < (long long)length ? (*channels[c])[(size_t)sample] : 0.0f;
            }
            for (int t = 0; t < STEM_FRAMES; t++){
                for (int i = 0; i < N_FFT; i++){
                    // Centered on the frame's time: past the chunk's ends, the sound mirrored
                    int k = t * HOP + i - N_FFT / 2;
                    if (k < 0) k = -k;
                    if (k >= CHUNK) k = 2 * (CHUNK - 1) - k;
                    frame[i] = std::complex<float>(chunk[k] * window[i], 0.0f);
                }
                fft(frame, twiddles, false);
                float* real = input.data() + (size_t)(2 * c) * STEM_BINS * STEM_FRAMES;
                float* imaginary = input.data() + (size_t)(2 * c + 1) * STEM_BINS * STEM_FRAMES;
                for (int bin = 0; bin < STEM_BINS; bin++){
                    real[(size_t)bin * STEM_FRAMES + t] = frame[bin].real();
                    imaginary[(size_t)bin * STEM_FRAMES + t] = frame[bin].imag();
                }
            }
        }
        if (cancel) return false;
        if (!model(input, output)){
            if (error.empty()) error = "the model stopped";
            return false;
        }
        if ((int)output.size() != STEM_TENSOR){
            error = "the model gave back something of another size";
            return false;
        }
        // Back to sound: each frame's spectrum (the frequencies the model doesn't see left silent), overlapped and added
        for (int c = 0; c < 2; c++){
            std::fill(sound.begin(), sound.end(), 0.0f);
            const float* real = output.data() + (size_t)(2 * c) * STEM_BINS * STEM_FRAMES;
            const float* imaginary = output.data() + (size_t)(2 * c + 1) * STEM_BINS * STEM_FRAMES;
            for (int t = 0; t < STEM_FRAMES; t++){
                std::fill(frame.begin(), frame.end(), std::complex<float>(0.0f, 0.0f));
                for (int bin = 0; bin < STEM_BINS; bin++){
                    frame[bin] = std::complex<float>(real[(size_t)bin * STEM_FRAMES + t], imaginary[(size_t)bin * STEM_FRAMES + t]);
                    if (bin > 0) frame[N_FFT - bin] = std::conj(frame[bin]); // a real signal's spectrum mirrors itself
                }
                fft(frame, twiddles, true);
                for (int i = 0; i < N_FFT; i++) sound[t * HOP + i] += frame[i].real() / N_FFT * window[i];
            }
            for (int i = 0; i < KEPT; i++){
                int k = N_FFT / 2 + TRIM + i; // past the mirrored start, and the chunk's thrown-away start
                (*stems[c])[at + i] = sound[k] / std::max(weight[k], 1e-8f);
            }
        }
        if (progress) progress->store((float)(at + KEPT) / padded);
    }
    stemLeft.resize(length);
    stemRight.resize(length);
    return true;
}
