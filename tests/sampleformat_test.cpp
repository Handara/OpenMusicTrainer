#include "doctest/doctest.h"

#include "core/sampleformat.h"

#include <vector>

TEST_CASE("an interface's integer samples become floats from -1 to 1"){
    std::vector<float> out(3);
    // 16-bit, little-endian: full scale down, half up, zero
    const unsigned char int16[] = { 0x00, 0x80,  0x00, 0x40,  0x00, 0x00 };
    convertSamples(int16, SampleFormat::Int16, 3, out.data(), 1);
    CHECK(out == std::vector<float>{-1.0f, 0.5f, 0.0f});
    // 24-bit packed: the sign must carry over from the third byte
    const unsigned char int24[] = { 0x00, 0x00, 0x80,  0x00, 0x00, 0x40,  0xFF, 0xFF, 0xFF };
    convertSamples(int24, SampleFormat::Int24, 3, out.data(), 1);
    CHECK(out[0] == -1.0f);
    CHECK(out[1] == 0.5f);
    CHECK(out[2] == doctest::Approx(-1.0f / 8388608.0f));
    // 32-bit, as a Focusrite driver sends it
    const unsigned char int32[] = { 0x00, 0x00, 0x00, 0x80,  0x00, 0x00, 0x00, 0x40,  0x00, 0x00, 0x00, 0xC0 };
    convertSamples(int32, SampleFormat::Int32, 3, out.data(), 1);
    CHECK(out == std::vector<float>{-1.0f, 0.5f, -0.5f});
}

TEST_CASE("32-bit words holding fewer bits, and floats"){
    std::vector<float> out(2);
    // 24 bits in a 32-bit word: 0x400000 is half scale, 0xFFC00000 (sign-extended) minus half
    const unsigned char in24[] = { 0x00, 0x00, 0x40, 0x00,  0x00, 0x00, 0xC0, 0xFF };
    convertSamples(in24, SampleFormat::Int32In24, 2, out.data(), 1);
    CHECK(out == std::vector<float>{0.5f, -0.5f});
    const float floats[] = { 0.25f, -0.75f };
    convertSamples(floats, SampleFormat::Float32, 2, out.data(), 1);
    CHECK(out == std::vector<float>{0.25f, -0.75f});
    const double doubles[] = { 0.125, -1.0 };
    convertSamples(doubles, SampleFormat::Float64, 2, out.data(), 1);
    CHECK(out == std::vector<float>{0.125f, -1.0f});
}

TEST_CASE("each input's buffer lands in its place among interleaved frames"){
    // Two inputs, arriving one buffer each (as ASIO hands them over), become frames: in1 in2, in1 in2...
    const unsigned char input1[] = { 0x00, 0x40,  0x00, 0x20 }; // 0.5, 0.25
    const unsigned char input2[] = { 0x00, 0xC0,  0x00, 0xE0 }; // -0.5, -0.25
    std::vector<float> frames(4, 9.0f);
    convertSamples(input1, SampleFormat::Int16, 2, frames.data(), 2);
    convertSamples(input2, SampleFormat::Int16, 2, frames.data() + 1, 2);
    CHECK(frames == std::vector<float>{0.5f, -0.5f, 0.25f, -0.25f});
}

TEST_CASE("sizes of each format"){
    CHECK(sampleBytes(SampleFormat::Int16) == 2);
    CHECK(sampleBytes(SampleFormat::Int24) == 3);
    CHECK(sampleBytes(SampleFormat::Int32In20) == 4);
    CHECK(sampleBytes(SampleFormat::Float64) == 8);
}
