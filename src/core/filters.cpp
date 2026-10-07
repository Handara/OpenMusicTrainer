#include "core/filters.h"

#include <algorithm>
#include <cmath>

const float PI_F = 3.14159265f;

// RBJ's cookbook filters
void setLowPass(Biquad& f, float frequency, float q, float rate){
    float w = 2.0f * PI_F * std::min(frequency, rate * 0.45f) / rate, c = std::cos(w), alpha = std::sin(w) / (2.0f * q);
    float a0 = 1.0f + alpha;
    f.b0 = (1.0f - c) / 2.0f / a0; f.b1 = (1.0f - c) / a0; f.b2 = f.b0;
    f.a1 = -2.0f * c / a0; f.a2 = (1.0f - alpha) / a0;
}

void setHighPass(Biquad& f, float frequency, float q, float rate){
    float w = 2.0f * PI_F * std::min(frequency, rate * 0.45f) / rate, c = std::cos(w), alpha = std::sin(w) / (2.0f * q);
    float a0 = 1.0f + alpha;
    f.b0 = (1.0f + c) / 2.0f / a0; f.b1 = -(1.0f + c) / a0; f.b2 = f.b0;
    f.a1 = -2.0f * c / a0; f.a2 = (1.0f - alpha) / a0;
}

void setPeak(Biquad& f, float frequency, float q, float gainDb, float rate){
    float A = std::pow(10.0f, gainDb / 40.0f), w = 2.0f * PI_F * std::min(frequency, rate * 0.45f) / rate;
    float c = std::cos(w), alpha = std::sin(w) / (2.0f * q), a0 = 1.0f + alpha / A;
    f.b0 = (1.0f + alpha * A) / a0; f.b1 = -2.0f * c / a0; f.b2 = (1.0f - alpha * A) / a0;
    f.a1 = -2.0f * c / a0; f.a2 = (1.0f - alpha / A) / a0;
}

void setShelf(Biquad& f, float frequency, float gainDb, bool high, float rate){
    float A = std::pow(10.0f, gainDb / 40.0f), w = 2.0f * PI_F * std::min(frequency, rate * 0.45f) / rate;
    float c = std::cos(w), s = std::sin(w), alpha = s / 2.0f * std::sqrt(2.0f), root = 2.0f * std::sqrt(A) * alpha;
    if (high){
        float a0 = (A + 1) - (A - 1) * c + root;
        f.b0 = A * ((A + 1) + (A - 1) * c + root) / a0; f.b1 = -2 * A * ((A - 1) + (A + 1) * c) / a0; f.b2 = A * ((A + 1) + (A - 1) * c - root) / a0;
        f.a1 = 2 * ((A - 1) - (A + 1) * c) / a0; f.a2 = ((A + 1) - (A - 1) * c - root) / a0;
    } else {
        float a0 = (A + 1) + (A - 1) * c + root;
        f.b0 = A * ((A + 1) - (A - 1) * c + root) / a0; f.b1 = 2 * A * ((A - 1) - (A + 1) * c) / a0; f.b2 = A * ((A + 1) - (A - 1) * c - root) / a0;
        f.a1 = -2 * ((A - 1) + (A + 1) * c) / a0; f.a2 = ((A + 1) + (A - 1) * c - root) / a0;
    }
}

