#include "core/tone.h"

#include <algorithm>
#include <cmath>

const float DARKEST_HZ = 300.0f;   // the tone knob's low-pass, all the way down...
const float BRIGHTEST_HZ = 16000.0f; // ...and all the way up
const float MAX_DRIVE_GAIN = 30.0f;  // how hard the signal is pushed into the saturation, drive at full
const float DC_POLE = 0.9995f;       // the DC blocker: a high-pass around 4 Hz at 48 kHz, far under a bass's low E (41 Hz)

void processTone(ToneState& state, const ToneSettings& settings, float* samples, int count, int sampleRate){
    const float drive = std::clamp(settings.drive, 0.0f, 1.0f);
    const float gain = 1.0f + drive * (MAX_DRIVE_GAIN - 1.0f);
    // tanh(gain * x), scaled so a full-scale input still reaches full scale: quiet playing is pushed up and loud
    // playing rounded off, as an amp's saturation does. Clean (drive 0) is left linear.
    const float saturation = std::tanh(gain);
    const float cutoff = DARKEST_HZ * std::pow(BRIGHTEST_HZ / DARKEST_HZ, std::clamp(settings.tone, 0.0f, 1.0f));
    const float smoothing = 1.0f - std::exp(-2.0f * 3.14159265f * cutoff / sampleRate); // a one-pole low-pass
    const float volume = std::clamp(settings.volume, 0.0f, 1.0f);
    for (int i = 0; i < count; i++){
        float x = samples[i];
        float blocked = x - state.dcIn + DC_POLE * state.dcOut; // no DC: it would only eat into the headroom
        state.dcIn = x;
        state.dcOut = blocked;
        float driven = drive > 0.0f ? std::tanh(gain * blocked) / saturation : blocked;
        state.lowPass += smoothing * (driven - state.lowPass);
        samples[i] = std::clamp(state.lowPass * volume, -1.0f, 1.0f); // never past full scale, whatever the settings
    }
}
