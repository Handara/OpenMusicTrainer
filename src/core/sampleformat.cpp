#include "core/sampleformat.h"

#include <cstdint>
#include <algorithm>
#include <cmath>
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

static void write32(unsigned char* p, int32_t value){
    uint32_t v = (uint32_t)value;
    p[0] = (unsigned char)v; p[1] = (unsigned char)(v >> 8); p[2] = (unsigned char)(v >> 16); p[3] = (unsigned char)(v >> 24);
}

// A float from -1 to 1 as an integer of `bits` bits, rounded, clipped at full scale
static int32_t toInteger(float value, int bits){
    const double full = (double)(1u << (bits - 1));
    double scaled = std::round(std::clamp((double)value, -1.0, 1.0) * full);
    return (int32_t)std::clamp(scaled, -full, full - 1.0);
}

void convertToFormat(const float* in, int stride, SampleFormat format, int count, void* out){
    unsigned char* bytes = (unsigned char*)out;
    switch (format){
        case SampleFormat::Int16:
            for (int i = 0; i < count; i++, bytes += 2){
                int32_t v = toInteger(in[(long long)i * stride], 16);
                bytes[0] = (unsigned char)v; bytes[1] = (unsigned char)(v >> 8);
            }
            break;
        case SampleFormat::Int24:
            for (int i = 0; i < count; i++, bytes += 3){
                int32_t v = toInteger(in[(long long)i * stride], 24);
                bytes[0] = (unsigned char)v; bytes[1] = (unsigned char)(v >> 8); bytes[2] = (unsigned char)(v >> 16);
            }
            break;
        case SampleFormat::Int32:
            for (int i = 0; i < count; i++, bytes += 4) write32(bytes, toInteger(in[(long long)i * stride], 32));
            break;
        case SampleFormat::Float32:
            for (int i = 0; i < count; i++, bytes += 4) std::memcpy(bytes, &in[(long long)i * stride], 4);
            break;
        case SampleFormat::Float64:
            for (int i = 0; i < count; i++, bytes += 8){
                double v = in[(long long)i * stride];
                std::memcpy(bytes, &v, 8);
            }
            break;
        case SampleFormat::Int32In16:
        case SampleFormat::Int32In18:
        case SampleFormat::Int32In20:
        case SampleFormat::Int32In24: {
            int bits = format == SampleFormat::Int32In16 ? 16 : format == SampleFormat::Int32In18 ? 18 : format == SampleFormat::Int32In20 ? 20 : 24;
            for (int i = 0; i < count; i++, bytes += 4) write32(bytes, toInteger(in[(long long)i * stride], bits));
            break;
        }
    }
}
