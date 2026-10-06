#pragma once

#include "core/lesson.h"
#include "learn/exercise.h"
#include "learn/lessonview.h"
#include "screens/gameplay.h"

#include <memory>
#include <string>
#include <vector>

// A lesson, step by step: Previous and Next (the arrows, Enter), dots showing where you are, each step drawn by
// lessonview. A step with a goal (an exercise, a song to play) starts as it's reached and runs inside the lesson,
// with a small bar showing the goal; once it's met, the lesson goes on by itself (Esc goes back to the lesson). A
// passed step stays passed. Progress is saved as you go, and the lesson reopens where you were.
class LessonPlayer : public Exercise {
public:
    // `stepExercises`: for each step, the exercise it runs (a copy; empty for other steps)
    // `playOptions`: how play steps play (the player's gameplay settings)
    LessonPlayer(const LessonEntry& entry, std::vector<ExerciseEntry> stepExercises, ExerciseFactory create,
                 const GameplayOptions& playOptions, const std::string& progressPath);
    ~LessonPlayer() override;

    void update() override;
    void draw() override;
    bool wantsToLeave() const override { return leave; }
    bool back() override; // a step running: back to the lesson

private:
    const LessonStep& step() const { return lesson.steps[current]; }
    bool hasGoal() const;
    int goal() const;
    bool canGoOn() const;           // the step's goal is met, or it has none
    void goTo(int index);
    void goOn();                    // the next step (an exercise starts at once), or the lesson finished
    void startStep();               // runs the step's exercise, or plays its song
    void stopStep();
    void save();
    void drawDots();
    void drawGoalBar();

    Lesson lesson;
    std::string folder;
    std::vector<ExerciseEntry> stepExercises;
    ExerciseFactory create;
    GameplayOptions playOptions;
    std::string progressPath;
    LessonProgress progress;
    std::string saveError;

    int current = 0;
    LessonMedia media;
    std::unique_ptr<Exercise> running; // the step's exercise, while it runs
    double passedAt = -1.0;            // when the running step's goal was met: the lesson goes on a moment later
    bool leave = false;
};
