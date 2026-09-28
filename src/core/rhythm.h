#pragma once

#include "core/drill.h"

#include <random>
#include <string>
#include <vector>

// Rhythm drills: a new rhythm every pass, read from the staff and played in time on one note. Rhythms are built a
// beat at a time from "cells", the beat-long figures rhythm is read in: a quarter, two eighths, an eighth rest and
// an eighth... An exercise picks which cells appear, so it can start with quarters and eighths and grow from there.

struct RhythmCell {
    const char* name;           // as exercise files write it: "eighths"
    std::vector<double> onsets; // where the notes start in the beat, 0 to under 1; empty = a quarter rest
};

// Every cell there is, in order of difficulty
const std::vector<RhythmCell>& rhythmCells();
const RhythmCell* findRhythmCell(const std::string& name);

struct RhythmConfig {
    std::vector<std::string> cells = { "quarter", "eighths", "rest" };
    int bars = 2;
    int beatsPerBar = 4;        // x/4 time
    DrillTempo tempo{ 60, 140, 4, 90 };
    std::vector<int> tuning = { 40, 45, 50, 55, 59, 64 }; // see rhythmString
};

// The string a rhythm is played on, open: the one written nearest the staff's middle line, where a rhythm reads
// most easily (a guitar's B string, written right on it)
int rhythmString(const std::vector<int>& tuning);

// One pass: a random cell for every beat, with at least one note in every bar
std::vector<DrillNote> buildRhythm(const RhythmConfig& config, std::mt19937& rng);
