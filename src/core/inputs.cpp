#include "core/inputs.h"

const float BASS_BELOW_HZ = 65.0f;     // between a bass's low E (41 Hz) and a guitar's (82 Hz), below a drop D (73 Hz)
const float GUITAR_BELOW_HZ = 100.0f;  // a guitar's lowest string, even tuned up a little

void takeChannel(const float* interleaved, int frames, int channels, int channel, float* out){
    if (channels <= 1){
        for (int i = 0; i < frames; i++) out[i] = interleaved[i];
        return;
    }
    for (int i = 0; i < frames; i++){
        const float* frame = interleaved + (size_t)i * channels;
        if (channel >= 0 && channel < channels){
            out[i] = frame[channel];
        } else {
            float sum = 0.0f;
            for (int c = 0; c < channels; c++) sum += frame[c];
            out[i] = sum / channels;
        }
    }
}

const char* inputRoleName(InputRole role){
    switch (role){
        case InputRole::Guitar: return "Guitar";
        case InputRole::Bass:   return "Bass";
        case InputRole::Voice:  return "Voice";
    }
    return "Guitar";
}

std::string guessInstrument(float lowestFrequency){
    if (lowestFrequency <= 0.0f) return "nothing yet";
    if (lowestFrequency < BASS_BELOW_HZ) return "a bass";
    if (lowestFrequency < GUITAR_BELOW_HZ) return "a guitar";
    return "a voice, or a higher instrument";
}

bool fitsRole(InputRole role, float lowestFrequency){
    if (lowestFrequency <= 0.0f) return true; // nothing heard: nothing to say
    switch (role){
        case InputRole::Bass:   return lowestFrequency < BASS_BELOW_HZ;
        case InputRole::Guitar: return lowestFrequency >= BASS_BELOW_HZ && lowestFrequency < GUITAR_BELOW_HZ;
        case InputRole::Voice:  return lowestFrequency >= BASS_BELOW_HZ; // a voice can't sing a bass's E1
    }
    return true;
}
