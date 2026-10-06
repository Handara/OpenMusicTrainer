#pragma once

#include "core/chart.h"
#include "core/notation.h"
#include "core/notequiz.h"
#include "core/score.h"
#include "core/settings.h"
#include "learn/exercise.h"
#include "ui/fretboardview.h"
#include "views/playnote.h"

#include <random>
#include <string>
#include <vector>

// Play this note (core/notequiz): a few notes asked one at a time, no clock, on play mode's neck. A note is asked by
// where it's played (the card lit: "Play the open high E string"), by its name, or written on the staff; played
// right, the next; played wrong, what it was and where the right one is. A run of them passes with enough right the
// first time, and the crowd cheers. The smallest step there is, for someone who's never played.
class NoteQuizExercise : public Exercise {
public:
    NoteQuizExercise(const std::string& title, const NoteQuizConfig& config, const KeySignature& key, bool onBass,
                     const std::string& progressPath, const Settings& settings);
    ~NoteQuizExercise() override;
    void update() override;
    void draw() override;
    bool wantsToLeave() const override { return false; } // Esc (learnBack) ends it
    int lessonScore() const override { return passedNow; } // runs passed, this time
    bool takeFinishedRun(int& percent) override;
    bool scoresRuns() const override { return true; }

private:
    void startRun();
    void played(int pitch);
    void finish();
    std::string promptText() const;
    void drawProgress(float left, float right, float top, float s);
    void drawStaffPrompts(float left, float top, float width, float height);
    void drawNeck(float left, float right, float top, float bottom, float s);

    std::string title;
    NoteQuizConfig config;
    KeySignature key;
    bool onBass;
    std::string progressPath;
    Settings settings;
    NoteQuizStats stats;
    NoteQuizRun run;
    std::mt19937 random;
    bool finished = false, passed = false;
    int passedNow = 0;
    int finishedPercent = -1; // a run just ended, its score: until it's taken
    double rightAt = -100.0, wrongAt = -100.0, finishedAt = -100.0;
    NeckStep lastRight{ -1, -1, -1 }; // the note just played right: its ring
    // The prompts written down, for the staff: a bar of four at a time, the one now lit
    Chart chart;
    Score score;
    std::vector<PlayNote> staffNotes;
    float shownTime = 0.0f; // eases to the note now: the page turns smoothly
    std::vector<float> cheer;
    FretboardLayout board;  // as last drawn: for clicks
    bool listening = false;
    std::string inputError;
};
