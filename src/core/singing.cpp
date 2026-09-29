#include "core/singing.h"

#include <cmath>
#include <vector>

int nextSingingNote(const SingingConfig& config, std::mt19937& rng, int last){
    std::vector<int> notes;
    for (int pitch = config.lowest; pitch <= config.highest; pitch++){
        int pitchClass = pitch % 12;
        bool natural = pitchClass == 0 || pitchClass == 2 || pitchClass == 4 || pitchClass == 5 || pitchClass == 7
                    || pitchClass == 9 || pitchClass == 11;
        if (config.naturalsOnly && !natural) continue;
        if (pitch != last) notes.push_back(pitch);
    }
    if (notes.empty()) return last;
    std::uniform_int_distribution<int> pick(0, (int)notes.size() - 1);
    return notes[pick(rng)];
}

float singingErrorCents(const SingingConfig& config, int target, float sungMidi){
    float semitones = sungMidi - (float)target;
    if (config.anyOctave) semitones -= 12.0f * std::round(semitones / 12.0f); // to the target's nearest octave
    return semitones * 100.0f;
}

bool holdPitch(PitchHold& hold, const SingingConfig& config, int target, float sungMidi, double seconds){
    bool inTune = sungMidi >= 0.0f && std::fabs(singingErrorCents(config, target, sungMidi)) <= config.toleranceCents;
    hold.inTuneFor = inTune ? hold.inTuneFor + seconds : 0.0;
    return hold.inTuneFor >= config.holdSeconds - 1e-6; // frame times summed never land exactly on it
}
