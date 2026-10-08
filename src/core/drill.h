#pragma once

#include "core/chart.h"
#include "core/scales.h"

#include <string>
#include <vector>

// Drills: notes played in time with a metronome, pass after pass, faster each time they're played cleanly. A scale
// drill plays a scale; a rhythm drill (core/rhythm) a new rhythm each pass. The logic only: which notes, and how the
// tempo moves. Playing it is the learn screen's job.

enum class DrillDirection { Up, Down, UpDown };

// How a drill's tempo moves, whatever it plays
struct DrillTempo {
    int startTempo = 60;                // bpm
    int maxTempo = 160;
    int tempoStep = 4;
    int passPercent = 90;               // a pass this accurate or better is clean: the tempo goes up
    int challengeTempo = 0;             // a clean pass at this tempo or faster passes the drill (a course's); 0: the start
};
// The tempo a clean pass must be played at, at least, to pass the drill: below it, it's practice
inline int drillChallengeTempo(const DrillTempo& rules){ return rules.challengeTempo > 0 ? rules.challengeTempo : rules.startTempo; }

struct ScaleDrillConfig {
    int rootPitchClass = 7;             // G
    std::string scale = "major";        // a name from allScales()
    int octaves = 2;
    Fingering fingering = Fingering::Position;
    int position = -1;                  // index finger's fret; -1 = one below the root on the lowest string
    DrillDirection direction = DrillDirection::UpDown;
    int notesPerBeat = 2;               // 1 = quarter notes, 2 = eighths, 3 = triplets, 4 = sixteenths
    DrillTempo tempo;
    std::vector<int> tuning = { 40, 45, 50, 55, 59, 64 }; // standard guitar
};

struct DrillNote {
    double beat;     // when, counted in beats from the start of the first bar
    int stringIndex;
    int fret;
    int pitch;
    double length = 0.0; // how long it's written, in beats; 0: until the next note (legato, a scale's)
};

// The notes of one pass; false (with a reason) if the scale doesn't fit this fingering or tuning
bool buildScaleDrill(const ScaleDrillConfig& config, std::vector<DrillNote>& out, std::string& error);

// The key a scale drill is written in: the scale's own (E minor: one sharp)
KeySignature scaleDrillKey(const ScaleDrillConfig& config);

// One pass as a chart, so it's written down (and timed) like any song: in the key and meter given (x/4), ending with
// the bar of the last note. Starts at 60 bpm with no offset: set the tempo and offset for each pass.
Chart drillChart(const std::vector<DrillNote>& notes, const std::vector<int>& tuning, const KeySignature& key, int beatsPerBar = 4);

// A piano, played as the drills see an instrument: one "string" tuned to 0, so each note's fret is its pitch (as play
// mode scores keys parts)
inline bool isPianoTuning(const std::vector<int>& tuning){ return tuning.size() == 1 && tuning[0] == 0; }

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

// The tempo to play next: where it got to, kept within the drill's tempos (they may have changed since), from the
// slowest it slows down to up to its max
int drillTempo(const DrillTempo& rules, const DrillProgress& progress);
const int DRILL_SLOW_DOWN = 5; // bpm, after a pass that isn't clean
// The slowest a drill goes, slowing down: 30 bpm under its start (never under 40)
int slowestDrillTempo(const DrillTempo& rules);

// After a pass played at `tempo`: clean (accuracy at least the pass mark) records the best tempo and speeds up
// one step; not clean, it slows down 5 bpm (to slowestDrillTempo at the least).
DrillPassOutcome finishDrillPass(const DrillTempo& rules, DrillProgress& progress, int tempo, float accuracyPercent);

DrillProgress loadDrillProgress(const std::string& path); // lenient, like all progress files
bool saveDrillProgress(const std::string& path, const DrillProgress& progress, std::string& error);
