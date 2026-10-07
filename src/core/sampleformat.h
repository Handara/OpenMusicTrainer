#pragma once

// Audio interfaces hand over samples in their own formats: 16, 24 or 32-bit integers, floats, or 32-bit words
// holding fewer bits. These turn them into the floats (-1 to 1) the rest of hardthz works with. Little-endian, as on
// every PC. Pure: the driver-facing code (audio/asiodriver) only says which format it got.

enum class SampleFormat {
    Int16,
    Int24,       // packed: three bytes a sample
    Int32,
    Float32,
    Float64,
    Int32In16,   // a 32-bit word holding a 16-bit sample in its low bits (and 18, 20, 24 below)
    Int32In18,
    Int32In20,
    Int32In24,
};

int sampleBytes(SampleFormat format);

// Converts `count` samples of one channel into floats, writing every `stride`th float of `out`: one input's buffer
// into its place in interleaved frames (stride = the number of inputs)
void convertSamples(const void* in, SampleFormat format, int count, float* out, int stride);

// The other way, for an interface's outputs: `count` floats, every `stride`th from `in` (one output's channel out of
// interleaved frames), into the interface's format. Out of range is clipped, never wrapped round.
void convertToFormat(const float* in, int stride, SampleFormat format, int count, void* out);
