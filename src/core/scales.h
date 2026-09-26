#pragma once

#include <string>
#include <vector>

// Scales, and where to play them on a fretted instrument.

struct ScaleInfo {
    const char* name;        // as written in exercise files: "major", "minor_pentatonic"...
    const char* displayName; // "Major", "Minor pentatonic"...
    std::vector<int> steps;  // semitones above the root, within one octave, e.g. major = 0 2 4 5 7 9 11
};

const std::vector<ScaleInfo>& allScales();
const ScaleInfo* findScale(const std::string& name); // nullptr if unknown

// "C", "F#", "Bb", "c#" -> 0..11 (C = 0); false if it isn't a note name
bool parsePitchClass(const std::string& name, int& pitchClass);

// The scale's notes from `lowest` to `highest` (MIDI, both included), ascending
std::vector<int> scalePitches(int rootPitchClass, const ScaleInfo& scale, int lowest, int highest);

struct FretPosition {
    int stringIndex; // 0 = lowest string
    int fret;
};

// How to lay notes out on the neck:
//   Position: the hand stays put, index finger on fret `position`, pinky three frets up (for position 0: open
//   strings and frets 1 to 4). A note moves to the next string when it would need a stretch past the pinky.
//   ThreeNotesPerString: three notes on each string, low to high.
enum class Fingering { Position, ThreeNotesPerString };

// Places ascending pitches on the neck; false (with a reason) if one can't be placed
bool fingerPitches(const std::vector<int>& pitches, const std::vector<int>& tuning, Fingering fingering, int position,
                   std::vector<FretPosition>& out, std::string& error);
