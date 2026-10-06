#pragma once

#include "core/drill.h"
#include "core/rhythm.h"

#include <random>
#include <string>
#include <vector>

// Sight reading drills: a new short melody every pass, read from the staff and played in time. The notes come from
// a key's scale, found in one position on the neck (a few frets on some strings), and move mostly by step with the
// odd leap, the way melodies do; the rhythm is built like a rhythm drill's.

struct ReadingConfig {
    int rootPitchClass = 0;         // C
    std::string scale = "major";    // a name from allScales()
    int lowestFret = 0;             // the position: every note is found in these frets...
    int highestFret = 3;
    std::vector<int> strings;       // ...on these strings (0 = lowest); empty = all of them
    int maxLeap = 2;                // the widest move, in notes of the scale (1 = by step only)
    std::vector<std::string> cells = { "quarter", "eighths", "rest" }; // the rhythm, as a rhythm drill's
    int bars = 2;
    int beatsPerBar = 4;
    DrillTempo tempo{ 50, 120, 4, 90 };
    std::vector<int> tuning = { 40, 45, 50, 55, 59, 64 };
    // Or just these notes (MIDI), at random (no walk to follow): each where it's lowest on the neck. The key still
    // writes the staff's signature; frets, strings and leap don't count then.
    std::vector<int> pool;
};

// The notes the position holds, low to high, each where it's played: for a note found twice, the lower fret
// (in open position, the open string). Empty if the scale has no note there.
std::vector<DrillNote> readingPositionNotes(const ReadingConfig& config);

// One pass. False (with a reason) if the position holds fewer than two of the scale's notes, or a note of the pool
// isn't on the neck.
bool buildReading(const ReadingConfig& config, std::mt19937& rng, std::vector<DrillNote>& out, std::string& error);
