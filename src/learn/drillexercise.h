#pragma once

#include "core/drill.h"
#include "core/judge.h"
#include "core/score.h"
#include "core/settings.h"
#include "learn/exercise.h"

#include <functional>
#include <string>
#include <vector>

// What a drill plays, and how
struct DrillSetup {
    std::string about;          // under the title: "Natural minor in E", "Quarters, eighths and rests"
    DrillTempo tempo;
    std::vector<int> tuning;
    KeySignature key;
    int beatsPerBar = 4;        // x/4
    bool timingOnly = false;    // any number key or played note counts: only when it's played is judged (rhythm)
    bool staffOnly = false;     // sheet music only, whatever the settings show (sight reading: no tab to read instead)
    std::function<std::vector<DrillNote>()> nextPass; // the notes of each pass: a scale's are the same every time,
                                                      // a rhythm's new
};

// A drill: notes scroll by in time with a metronome, pass after pass, faster each time they're played cleanly.
// Judged from the number keys or the player's instrument; the best clean tempo is saved.
class DrillExercise : public Exercise {
public:
    DrillExercise(const std::string& title, const DrillSetup& setup, const std::string& progressPath, const Settings& settings);
    ~DrillExercise() override;

    void update() override;
    void draw() override;
    bool wantsToLeave() const override { return leave; }
    int lessonScore() const override { return cleanPassesNow; }
    bool takeFinishedRun(int& percent) override;
    bool scoresRuns() const override { return true; }
    bool goesOn() const override { return true; } // each clean pass, faster

private:
    void startPass(bool fresh); // fresh: new notes from the setup (not for the first pass: it plays what's shown)
    void placePass(double downbeat);
    void finishPass();
    double drillTime() const; // the audio clock, minus the output offset: what the notes are timed against

    std::string title;
    DrillSetup setup;
    std::string progressPath;
    Settings settings;
    DrillProgress progress;
    std::vector<DrillNote> drillNotes; // this pass, in beats
    Chart chart;                       // the same pass as a chart, timed for this pass

    bool running = false;          // Space starts and stops
    int tempo = 0;                 // of the current pass
    double countInStart = 0.0;     // audio time of the first count-in click
    double firstNoteTime = 0.0;    // audio time of the pass's first bar's first beat
    double passEndTime = 0.0;      // when the pass is over and judged
    int nextClick = 0;             // the next metronome click to schedule, counted from countInStart
    int totalClicks = 0;
    std::vector<PlayNote> notes;   // the pass's notes in audio time, for judging and the views
    Score score;                   // the pass written down, in audio time

    int hits = 0, perfects = 0;
    int cleanPassesNow = 0;        // clean passes since the drill was opened (a lesson's goal counts these)
    int finishedPercent = -1;      // a pass just ended, its share of notes hit: until it's taken
    std::string passText;          // the last pass's result
    std::string inputError;
    int lastPlayedPitch = -1;
    bool leave = false;
};
