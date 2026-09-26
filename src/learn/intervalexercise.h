#pragma once

#include "core/intervals.h"
#include "learn/exercise.h"

#include <string>

// Hear an interval, name it. Progress (unlocked intervals, stats) is saved after every answer.
class IntervalExercise : public Exercise {
public:
    IntervalExercise(IntervalDirection direction, const std::string& progressPath);
    ~IntervalExercise() override;

    void update() override;
    void draw() override;
    bool wantsToLeave() const override { return leave; }

private:
    void nextQuestion();
    void playInterval(int firstPitch, int secondPitch);
    void submit(int semitones);

    IntervalTrainer trainer;
    std::string progressPath;
    std::string saveError;

    IntervalQuestion question{};
    bool answered = false;     // showing feedback for `question`
    bool lastCorrect = false;
    int lastAnswer = 0;
    double answeredAt = 0.0;   // when, to move on by itself after a right answer
    int newlyUnlocked = 0;     // semitones of an interval just unlocked, shown until the next answer

    int sessionAsked = 0;
    int sessionCorrect = 0;
    bool leave = false;
};
