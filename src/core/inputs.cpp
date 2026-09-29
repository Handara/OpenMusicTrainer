#include "core/inputs.h"

#include <algorithm>

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

InputRole roleForTuning(int lowestPitch){
    return lowestPitch < 36 ? InputRole::Bass : InputRole::Guitar;
}

std::string guessInstrument(float lowestFrequency){
    if (lowestFrequency <= 0.0f) return "nothing yet";
    if (lowestFrequency < BASS_BELOW_HZ) return "a bass";
    if (lowestFrequency < GUITAR_BELOW_HZ) return "a guitar, or a low voice"; // a man's voice reaches G2
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

const float FLOOR_RISE_DB_PER_S = 3.0f;
const float FLOOR_RISE_PLAYING_DB_PER_S = 0.5f; // while played: a long note stays well above it for a minute
const float LOWEST_FLOOR_DB = -100.0f;   // nothing played is quieter: stops digital silence (exact zeros) dragging
                                         // the floor so low that hiss would seem played
const float SOUNDING_RISE_DB = 12.0f;    // a played note is at least this far above the input's floor
const float QUIETEST_PLAYED_DB = -75.0f; // below this nothing is played, however quiet the input is

void trackNoiseFloor(NoiseFloor& floor, float levelDb, float seconds){
    float quietest = std::max(levelDb, LOWEST_FLOOR_DB);
    if (!floor.started || quietest < floor.db){
        floor.db = quietest;
        floor.started = true;
        return;
    }
    float rise = isSounding(floor, levelDb) ? FLOOR_RISE_PLAYING_DB_PER_S : FLOOR_RISE_DB_PER_S;
    floor.db = std::min(quietest, floor.db + rise * seconds);
}

float riseAboveFloor(const NoiseFloor& floor, float levelDb){
    if (!floor.started || levelDb < QUIETEST_PLAYED_DB) return 0.0f;
    return std::max(0.0f, levelDb - floor.db);
}

bool isSounding(const NoiseFloor& floor, float levelDb){
    return riseAboveFloor(floor, levelDb) >= SOUNDING_RISE_DB;
}

const float TAKEN_PENALTY_DB = 12.0f;

int playedInput(const std::vector<float>& rises, const std::vector<bool>& taken){
    int played = -1;
    float best = 0.0f;
    for (int c = 0; c < (int)rises.size(); c++){
        if (rises[c] <= 0.0f) continue;
        float score = rises[c] - (c < (int)taken.size() && taken[c] ? TAKEN_PENALTY_DB : 0.0f);
        if (played < 0 || score > best){
            played = c;
            best = score;
        }
    }
    return played;
}
