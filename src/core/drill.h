#pragma once

#include "core/chart.h"
#include "core/scales.h"

#include <string>
#include <vector>

// Scale drills: a scale played in time with a metronome, over and over, faster each time it's played cleanly.
// The logic only: which notes, and how the tempo moves. Playing it is the learn screen's job.

enum class DrillDirection { Up, Down, UpDown };

struct ScaleDrillConfig {
    int rootPitchClass = 7;             // G
    std::string scale = "major";        // a name from allScales()
    int octaves = 2;
    Fingering fingering = Fingering::Position;
    int position = -1;                  // index finger's fret; -1 = one below the root on the lowest string
    DrillDirection direction = DrillDirection::UpDown;
    int notesPerBeat = 2;               // 1 = quarter notes, 2 = eighths, 3 = triplets, 4 = sixteenths
    int startTempo = 60;                // bpm
    int maxTempo = 160;
    int tempoStep = 4;
    int passPercent = 90;               // a pass this accurate or better is clean: the tempo goes up
    std::vector<int> tuning = { 40, 45, 50, 55, 59, 64 }; // standard guitar
};

struct DrillNote {
    double beat;     // when, counted in beats from the first note
    int stringIndex;
    int fret;
    int pitch;
};

// The notes of one pass; false (with a reason) if the scale doesn't fit this fingering or tuning
bool buildScaleDrill(const ScaleDrillConfig& config, std::vector<DrillNote>& out, std::string& error);

// One pass as a chart, so it's written down (and timed) like any song: 4/4 in the scale's key, ending with the
// bar of the last note. Starts at 60 bpm with no offset: set the tempo and offset for each pass.
Chart drillChart(const ScaleDrillConfig& config, const std::vector<DrillNote>& notes);

struct DrillProgress {
    int tempo = 0;          // the tempo to play next; 0 = not started (the config's start tempo)
    int bestCleanTempo = 0; // the fastest tempo passed cleanly: the muscle-memory number
    int passes = 0;
    int cleanPasses = 0;
};

struct DrillPassOutcome {
    bool clean;
    int nextTempo;
    bool newBest;
};

int drillTempo(const ScaleDrillConfig& config, const DrillProgress& progress);

// After a pass played at `tempo`: clean (accuracy at least the pass mark) records the best tempo and speeds up
// one step; below 50% slows down one step; in between, the tempo stays.
DrillPassOutcome finishDrillPass(const ScaleDrillConfig& config, DrillProgress& progress, int tempo, float accuracyPercent);

DrillProgress loadDrillProgress(const std::string& path); // lenient, like all progress files
bool saveDrillProgress(const std::string& path, const DrillProgress& progress, std::string& error);
