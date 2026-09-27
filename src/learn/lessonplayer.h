#pragma once

#include "core/lesson.h"
#include "learn/exercise.h"
#include "learn/lessonview.h"

#include <memory>
#include <string>
#include <vector>

// A lesson, step by step: Back and Next, dots showing where you are, each step drawn by lessonview. A step with a
// goal (an exercise, a song to play) runs inside the lesson, with a small bar showing the goal; Next unlocks once
// it's met, and a passed step stays passed. Progress is saved as you go, and the lesson reopens where you were.
class LessonPlayer : public Exercise {
public:
    // `stepExercises`: for each step, the exercise it runs (a copy; empty for other steps)
    LessonPlayer(const LessonEntry& entry, std::vector<ExerciseEntry> stepExercises, ExerciseFactory create,
                 const std::string& progressPath);
    ~LessonPlayer() override;

    void update() override;
    void draw() override;
    bool wantsToLeave() const override { return leave; }

private:
    const LessonStep& step() const { return lesson.steps[current]; }
    bool hasGoal() const;
    int goal() const;
    bool canGoOn() const;           // the step's goal is met, or it has none
    void goTo(int index);
    void startStep();               // runs the step's exercise
    void stopStep();
    void save();
    void drawDots();
    void drawGoalBar();

    Lesson lesson;
    std::string folder;
    std::vector<ExerciseEntry> stepExercises;
    ExerciseFactory create;
    std::string progressPath;
    LessonProgress progress;
    std::string saveError;

    int current = 0;
    LessonMedia media;
    std::unique_ptr<Exercise> running; // the step's exercise, while it runs
    bool leave = false;
};
