#pragma once

#include "core/drill.h"
#include "core/judge.h"
#include "core/score.h"
#include "core/settings.h"
#include "learn/exercise.h"

#include <string>
#include <vector>

// A scale drill: the scale scrolls by in time with a metronome, pass after pass, faster each time it's played
// cleanly. Judged from the number keys or the player's instrument; the best clean tempo is saved.
class DrillExercise : public Exercise {
public:
    DrillExercise(const std::string& title, const ScaleDrillConfig& config, const std::string& progressPath,
                  const Settings& settings);
    ~DrillExercise() override;

    void update() override;
    void draw() override;
    bool wantsToLeave() const override { return leave; }

private:
    void startPass();
    void finishPass();
    double drillTime() const; // the audio clock, minus the output offset: what the notes are timed against

    std::string title;
    ScaleDrillConfig config;
    std::string progressPath;
    Settings settings;
    DrillProgress progress;
    std::vector<DrillNote> drillNotes; // one pass, in beats
    Chart chart;                       // the same pass as a chart: its tempo and offset are set for each pass

    bool running = false;          // Space starts and stops
    int tempo = 0;                 // of the current pass
    double countInStart = 0.0;     // audio time of the first count-in click
    double firstNoteTime = 0.0;    // audio time of the pass's first note
    double passEndTime = 0.0;      // when the pass is over and judged
    int nextClick = 0;             // the next metronome click to schedule, counted from countInStart
    int totalClicks = 0;
    std::vector<PlayNote> notes;   // the pass's notes in audio time, for judging and the views
    Score score;                   // the pass written down, in audio time

    int hits = 0, perfects = 0;
    std::string passText;          // the last pass's result
    std::string inputError;
    int lastPlayedPitch = -1;
    bool leave = false;
};
