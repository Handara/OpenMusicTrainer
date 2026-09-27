#pragma once

#include "learn/exercise.h"
#include "screens/gameplay.h"

#include <string>

// A play step in a lesson: a chart from the lesson's folder, played and judged like any song. It drives the gameplay
// module (the same start, update, draw and stop the play screen uses), so a lesson song plays exactly like a real one.
// Its lesson score is the best percentage of notes hit in a finished run.
class PlayExercise : public Exercise {
public:
    PlayExercise(const std::string& chartPath, const GameplayOptions& options);
    ~PlayExercise() override;

    void update() override;
    void draw() override;
    bool wantsToLeave() const override { return leave; }
    int lessonScore() const override { return bestPercent; }

private:
    void start();

    std::string chartPath;
    GameplayOptions options;
    bool playing = false;
    GameResult last{};
    bool hasResult = false;
    int bestPercent = 0;
    std::string error;
    bool leave = false;
};
