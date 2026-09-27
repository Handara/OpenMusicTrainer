#pragma once

#include "core/exercisefile.h"
#include "core/routine.h"
#include "learn/exercise.h"

#include <memory>
#include <string>
#include <vector>

// A routine: its steps' exercises one after the other, each for a few minutes. A small bar in the top-right
// corner shows the time left and moves on. Time running out never cuts a step short: the player might be
// mid-scale, so it only says so and waits for "Next step". Finishing the last step counts toward the day streak.
class RoutineExercise : public Exercise {
public:
    struct Step {
        ExerciseEntry entry; // a copy: the learn screen's list is rebuilt while this runs
        double seconds;
    };
    RoutineExercise(const std::string& title, std::vector<Step> steps, const std::string& progressPath, ExerciseFactory create);

    void update() override;
    void draw() override;
    bool wantsToLeave() const override { return leave; }

private:
    void startStep(int index);
    void nextStep();
    void drawStepBar();
    void drawDone();

    std::string title;
    std::vector<Step> steps;
    std::string progressPath;
    ExerciseFactory create;
    RoutineProgress progress;

    int current = 0;                    // the step being practiced
    std::unique_ptr<Exercise> exercise; // its exercise; empty once the routine is done
    double stepStartedAt = 0.0;
    double practicedSeconds = 0.0;      // the finished steps' time
    bool done = false;
    std::string saveError;
    bool leave = false;
};
