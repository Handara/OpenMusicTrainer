#pragma once

#include "core/drill.h"

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

// The chord some notes make (MIDI pitches, any order), by its usual symbol: "C", "Am7", "Bdim", "E5". Over a bass
// that isn't its root, the bass after a slash: "C/E". Where the notes read two ways (C E G A is C6 and Am7), the one
// built on the bass wins. "" for notes that aren't a chord it knows.
std::string nameChord(const std::vector<int>& pitches);

// The 12 notes' share of a sound, C first, adding up to 1 (all zero for silence). From the energy at every
// semitone from E2 to E6 (a Goertzel filter each, over a Hann window), folded into the 12 note names.
std::array<float, 12> chroma(const float* samples, int count, int sampleRate);

// Whether a sound holds each of some notes (MIDI pitches), octave and all: for checking that the chord a song had
// due was played, bass double stops included, where a single-note detector hears a muddle. Each note's harmonics
// (it and the seven above it) are measured and must stand out against those of the notes a half step either side.
// Low notes need a longish sound to tell from their neighbours: from a pluck, CHORD_LISTEN_S.
const float CHORD_LISTEN_S = 0.16f;
bool soundHoldsNotes(const float* samples, int count, int sampleRate, const std::vector<int>& pitches);

// How well a chroma fits a chord: the share of the sound on the chord's notes, less what falls outside them
float chordFit(const std::array<float, 12>& notes, const ChordInfo& chord);

// The chord a sound holds, heard unasked (a strum): every root and kind (major, minor, 7, maj7, m7, sus2, sus4, power
// chord) is fitted to its chroma, the best taken if enough of the sound is on its notes. Its notes, as pitch classes
// above C (root first), in `pitchClasses`. "" for a sound that isn't a chord.
std::string recognizeChord(const std::array<float, 12>& notes, std::vector<int>* pitchClasses = nullptr);

// A chord change drill: chords played in turn, each for a few beats, strummed on the change; faster after each
// clean pass, like the other drills
struct ChordDrillConfig {
    std::vector<std::string> chords = { "Em", "C" }; // names from commonChords()
    int beatsPerChord = 4;
    int rounds = 2;                                  // times through the list in one pass
    DrillTempo tempo{ 60, 120, 4, 80 };
};

// Strums in a stream of samples: the jump in loudness a strum's attack makes (not each note in it). Loudness is
// measured in 10 ms frames; a strum is a frame well above the ones just before it, above a floor of noise.
struct StrumDetector {
    int sampleRate = 44100;
    int frameSize = 441;
    float frameSquares = 0.0f;       // the frame being measured
    int inFrame = 0;
    std::vector<float> recentLevels; // the last frames' levels (RMS), oldest first
    long long position = 0;          // samples fed so far
    long long lastStrum = -1000000;
};
void initStrumDetector(StrumDetector& detector, int sampleRate);
// Feeds more samples; appends where each strum found in them starts, as positions in the stream
void feedStrumDetector(StrumDetector& detector, const float* samples, int count, std::vector<long long>& strums);

// Whether the sound is the chord asked for: it fits it at least as well as every major and minor triad (the chords
// a wrong strum usually is), and enough of the sound is on its notes
bool soundsLikeChord(const std::array<float, 12>& notes, const ChordInfo& chord);
