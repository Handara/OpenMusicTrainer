#include "core/sampleformat.h"

#include <cstdint>
#include <cstring>

int sampleBytes(SampleFormat format){
    switch (format){
        case SampleFormat::Int16:   return 2;
        case SampleFormat::Int24:   return 3;
        case SampleFormat::Float64: return 8;
        default:                    return 4;
    }
}

// A little-endian integer read byte by byte: no alignment needed, and the same on any machine
static int32_t read32(const unsigned char* p){
    return (int32_t)((uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24);
}

// Each format in a loop of its own: this runs on the audio thread for every sample, so the format is decided once
void convertSamples(const void* in, SampleFormat format, int count, float* out, int stride){
    const unsigned char* bytes = (const unsigned char*)in;
    switch (format){
        case SampleFormat::Int16:
            for (int i = 0; i < count; i++, bytes += 2) out[(long long)i * stride] = (int16_t)(bytes[0] | bytes[1] << 8) / 32768.0f;
            break;
        case SampleFormat::Int24:
            for (int i = 0; i < count; i++, bytes += 3){
                int32_t value = (int32_t)((uint32_t)bytes[0] << 8 | (uint32_t)bytes[1] << 16 | (uint32_t)bytes[2] << 24) >> 8; // sign-extended
                out[(long long)i * stride] = value / 8388608.0f;
            }
            break;
        case SampleFormat::Int32:
            for (int i = 0; i < count; i++, bytes += 4) out[(long long)i * stride] = (float)(read32(bytes) / 2147483648.0);
            break;
        case SampleFormat::Float32:
            for (int i = 0; i < count; i++, bytes += 4) std::memcpy(&out[(long long)i * stride], bytes, 4);
            break;
        case SampleFormat::Float64:
            for (int i = 0; i < count; i++, bytes += 8){
                double value;
                std::memcpy(&value, bytes, 8);
                out[(long long)i * stride] = (float)value;
            }
            break;
        case SampleFormat::Int32In16:
        case SampleFormat::Int32In18:
        case SampleFormat::Int32In20:
        case SampleFormat::Int32In24: {
            int bits = format == SampleFormat::Int32In16 ? 16 : format == SampleFormat::Int32In18 ? 18 : format == SampleFormat::Int32In20 ? 20 : 24;
            float scale = 1.0f / (float)(1 << (bits - 1));
            for (int i = 0; i < count; i++, bytes += 4) out[(long long)i * stride] = read32(bytes) * scale;
            break;
        }
    }
}
