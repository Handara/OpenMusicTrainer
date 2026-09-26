#include "core/music.h"

#include <cmath>

const float A4_FREQUENCY = 440.0f;
const float A4_MIDI = 69.0f;

const char* const PITCH_CLASS_NAMES[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };

const char* pitchClassName(int midiPitch){
    return PITCH_CLASS_NAMES[((midiPitch % 12) + 12) % 12]; // stays in 0..11 even for negative input
}

int pitchOctave(int midiPitch){
    return (int)std::floor(midiPitch / 12.0) - 1;
}

// Each octave doubles the frequency and has 12 semitones, so semitones = 12 * log2(frequency ratio)
float frequencyToMidi(float frequency){
    return A4_MIDI + 12.0f * std::log2(frequency / A4_FREQUENCY);
}

float midiToFrequency(float midiPitch){
    return A4_FREQUENCY * std::exp2((midiPitch - A4_MIDI) / 12.0f);
}
