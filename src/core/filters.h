#pragma once

// Filters for working on sound sample by sample (the tone chain's effects, the amp models): a biquad, the second-order
// filter most of them are made of, and its usual shapes (Robert Bristow-Johnson's cookbook). Pure.

struct Biquad {
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    float z1 = 0, z2 = 0;
    float process(float x){
        float y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
};

// Each sets a biquad's coefficients, keeping its memory (so a knob turned doesn't click); the frequency is kept under
// the rate's Nyquist
void setLowPass(Biquad& f, float frequency, float q, float rate);
void setHighPass(Biquad& f, float frequency, float q, float rate);
void setPeak(Biquad& f, float frequency, float q, float gainDb, float rate);
void setShelf(Biquad& f, float frequency, float gainDb, bool high, float rate);
