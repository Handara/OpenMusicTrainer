#pragma once

#include <array>
#include <string>
#include <vector>

// Chords: their notes, where to play them, and hearing whether one was played. Hearing a chord in full (every note,
// transcribed) is hard; hearing whether it's the one that was asked for is much easier, and is what practice needs:
// the sound's chroma (how much of each of the 12 notes it holds) is compared with the chord asked for and with the
// other chords it could be.

struct ChordInfo {
    std::string name;                 // "Em", "G7", "Cmaj7"
    int rootPitchClass;               // 0 = C
    std::vector<int> intervals;       // semitones above the root: a minor triad is 0 3 7
    std::array<int, 6> shape;         // the open-position shape on a standard-tuned guitar, low E first: a fret,
                                      // or -1 for a string not played
};

// The chords every guitarist starts with, in their open shapes (and F, the first barre chord)
const std::vector<ChordInfo>& commonChords();
const ChordInfo* findChord(const std::string& name);

// The 12 notes' share of a sound, C first, adding up to 1 (all zero for silence). From the energy at every
// semitone from E2 to E6 (a Goertzel filter each, over a Hann window), folded into the 12 note names.
std::array<float, 12> chroma(const float* samples, int count, int sampleRate);

// How well a chroma fits a chord: the share of the sound on the chord's notes, less what falls outside them
float chordFit(const std::array<float, 12>& notes, const ChordInfo& chord);

// Whether the sound is the chord asked for: it fits it at least as well as every major and minor triad (the chords
// a wrong strum usually is), and enough of the sound is on its notes
bool soundsLikeChord(const std::array<float, 12>& notes, const ChordInfo& chord);
