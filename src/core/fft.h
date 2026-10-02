#pragma once

#include <complex>
#include <vector>

// The fast Fourier transform: a sound's samples in, how much of each frequency it holds out (and back). Radix 2, in
// place: the size is a power of two. The rotations it needs are made once and passed in, since the same size is
// usually transformed many times over. Pure math.

// e^(-2 pi i k / size), for k under size / 2
std::vector<std::complex<float>> fftTwiddles(int size);

// `data` transformed where it is, its size the twiddles'. The inverse isn't scaled: divide by the size.
void fft(std::vector<std::complex<float>>& data, const std::vector<std::complex<float>>& twiddles, bool inverse);
