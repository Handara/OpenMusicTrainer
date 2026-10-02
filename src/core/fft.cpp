#include "core/fft.h"

#include <cmath>
#include <utility>

std::vector<std::complex<float>> fftTwiddles(int size){
    const float PI_F = 3.14159265358979f;
    std::vector<std::complex<float>> twiddles(size / 2);
    for (int k = 0; k < size / 2; k++) twiddles[k] = std::polar(1.0f, -2.0f * PI_F * k / size);
    return twiddles;
}

void fft(std::vector<std::complex<float>>& data, const std::vector<std::complex<float>>& twiddles, bool inverse){
    const int n = (int)data.size();
    // Each value goes where its index, read backwards in binary, says
    for (int i = 1, j = 0; i < n; i++){
        int bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(data[i], data[j]);
    }
    // Then pairs are combined, then pairs of pairs, each twice as long
    for (int length = 2; length <= n; length <<= 1){
        const int half = length / 2, stride = n / length;
        for (int start = 0; start < n; start += length){
            for (int k = 0; k < half; k++){
                std::complex<float> w = twiddles[k * stride];
                if (inverse) w = std::conj(w);
                std::complex<float> a = data[start + k], b = data[start + k + half] * w;
                data[start + k] = a + b;
                data[start + k + half] = a - b;
            }
        }
    }
}
