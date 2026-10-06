#include "core/music.h"

#include <cctype>
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

bool parseNoteName(const std::string& text, int& pitch){
    const int LETTER_PITCHES[7] = { 9, 11, 0, 2, 4, 5, 7 }; // A B C D E F G
    if (text.size() < 2) return false;
    const char letter = (char)std::toupper((unsigned char)text[0]);
    if (letter < 'A' || letter > 'G') return false;
    int semitone = LETTER_PITCHES[letter - 'A'];
    size_t at = 1;
    if (text[at] == '#'){ semitone++; at++; }
    else if (text[at] == 'b'){ semitone--; at++; }
    if (at >= text.size()) return false;
    int octave = 0;
    bool negative = text[at] == '-';
    if (negative) at++;
    if (at >= text.size()) return false;
    for (; at < text.size(); at++){
        if (!std::isdigit((unsigned char)text[at])) return false;
        octave = octave * 10 + (text[at] - '0');
        if (octave > 10) return false;
    }
    pitch = ((negative ? -octave : octave) + 1) * 12 + semitone;
    return pitch >= 0 && pitch <= 127;
}
