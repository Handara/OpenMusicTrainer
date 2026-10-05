#pragma once

#include "core/necktrainer.h"
#include "core/settings.h"
#include "learn/exercise.h"

#include <string>
#include <vector>

// Learning the neck (core/necktrainer): a scale in one place on the neck, played up and down, in thirds or in triads,
// or one note on every string, at the player's own pace, on the instrument. Play mode's neck shows the shape, lights
// the next note and the way to it; a scoreboard counts the notes, the mistakes and the time against the best. Between
// runs the neck is a map of how quick each of its notes is to find. Everything can be changed as it goes: the key,
// the scale, the shape, the position, the pattern. Each run is saved (core/necktrainer: NeckStats).
class NeckExercise : public Exercise {
public:
    NeckExercise(const std::string& title, const NeckRoutine& routine, bool onBass, const std::string& progressPath,
                 const Settings& settings);
    ~NeckExercise() override;
    void update() override;
    void draw() override;
    bool wantsToLeave() const override { return leave; }
    int lessonScore() const override { return cleanRuns; } // runs with no mistake, this time

private:
    void restart();           // the routine's steps again, from the first
    void played(int pitch);   // a note heard (or a fret clicked)
    void finish();
    void drawControls(float left, float top, float s);
    void drawScoreboard(float right, float top, float s);
    void drawResults(float left, float top, float width, float s);

    std::string title;
    NeckRoutine routine;
    bool onBass;
    std::vector<int> tuning;
    int frets;
    std::string progressPath;
    Settings settings;
    NeckStats stats;
    std::vector<NeckStep> steps;
    std::string stepsError;   // the routine doesn't fit there
    NeckRun run;
    bool finished = false;
    double lastRightAt = -100.0, wrongAt = -100.0, finishedAt = -100.0;
    int cleanRuns = 0;
    bool listening = false;
    std::string inputError;
    bool leave = false;
};
