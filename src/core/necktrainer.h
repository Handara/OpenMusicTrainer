#pragma once

#include "core/scales.h"

#include <map>
#include <string>
#include <utility>
#include <vector>

// Learning the neck: a scale's notes in one place on the neck, played through in a pattern, at the player's own pace.
// Pure logic: which notes, in what order, where, and how a run of them is going.
//
// The patterns, each going up and then back down:
//   EveryString: one note (a pitch class) on every string, the lowest string to the highest and back, each the
//                place nearest the one before: where it is, wherever the hand is
//   Straight:    the scale through the place on the neck
//   Thirds:      in thirds, 1-3, 2-4, 3-5... (and down, 8-6, 7-5...)
//   Triads:      the scale's triads, 1-3-5, 2-4-6... (and down, 8-6-4...)
enum class NeckPattern { EveryString, Straight, Thirds, Triads };

struct NeckRoutine {
    int rootPitchClass = 0;               // the key: 0 = C
    std::string scale = "major";          // as in core/scales: "major", "minor_pentatonic"...
    Fingering fingering = Fingering::Position;
    int position = 0;                     // the index finger's fret (Position); the low string's first (3 per string)
    NeckPattern pattern = NeckPattern::Straight;
    int notePitchClass = 0;               // EveryString: the note to find on every string
};

struct NeckStep {
    int pitch;   // MIDI
    int string;  // 0 = lowest
    int fret;
};

// The notes a routine plays, in order; false (with a reason) when the scale doesn't fit there
bool neckSteps(const NeckRoutine& routine, const std::vector<int>& tuning, int frets, std::vector<NeckStep>& out,
               std::string& error);

// "C major, position 7, in thirds": for titles and for keeping each routine's stats apart
std::string neckRoutineName(const NeckRoutine& routine);
const char* neckPatternName(NeckPattern pattern);

// A run through a routine's steps: each note played is the next one or a mistake; the time each took is kept
struct NeckRun {
    std::vector<NeckStep> steps;
    size_t next = 0;                  // the step to play now
    int mistakes = 0;
    double startedAt = -1.0;          // the first right note (seconds, any clock)
    double lastAt = -1.0;             // the latest right one
    std::vector<float> stepSeconds;   // for each step played, how long after the one before it (the first: 0)
    int lastWrongPitch = -1;          // the latest mistake, for showing it
};
void startNeckRun(NeckRun& run, const std::vector<NeckStep>& steps);
// A note heard at `time`: true if it was the next step (exact pitch, octave and all), which it moves past
bool playNeckNote(NeckRun& run, int pitch, double time);
bool neckRunDone(const NeckRun& run);
double neckRunSeconds(const NeckRun& run); // from the first note to the last played

// What's kept of the runs, one file for an exercise: each finished run (its routine by name), and for every string and
// fret how long finding it took on average, every routine together (a map of what's quick to find and what isn't)
struct NeckRecord {
    std::string date;     // YYYY-MM-DD
    float seconds = 0.0f;
    int mistakes = 0;
    int notes = 0;
    std::string routine;  // neckRoutineName
};
struct NeckStats {
    std::vector<NeckRecord> runs;                                // oldest first
    std::map<std::pair<int, int>, std::pair<float, int>> cells;  // (string, fret) -> (seconds summed, notes)
};
NeckStats loadNeckStats(const std::string& path); // empty if there's none yet
bool saveNeckStats(const std::string& path, const NeckStats& stats, std::string& error);
// A finished run, added: its record, and each note's time to its place (the first note's isn't known: left out)
void addNeckRun(NeckStats& stats, const NeckRun& run, const std::string& routine, const std::string& date);
// The routine's runs, oldest first, and its best (fewest seconds of the runs with no mistake, else of all; -1: none)
std::vector<NeckRecord> neckRunsOf(const NeckStats& stats, const std::string& routine);
float neckBestSeconds(const NeckStats& stats, const std::string& routine);
