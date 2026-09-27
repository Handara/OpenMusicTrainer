#pragma once

#include "core/intervals.h"
#include "learn/exercise.h"

#include <string>

// Hear an interval, name it. Progress (unlocked intervals, stats) is saved after every answer.
class IntervalExercise : public Exercise {
public:
    IntervalExercise(const std::string& title, const IntervalConfig& config, const std::string& progressPath,
                     const std::string& inputDevice);
    ~IntervalExercise() override;

    void update() override;
    void draw() override;
    bool wantsToLeave() const override { return leave; }
    int lessonScore() const override { return trainer.streak; } // right answers in a row

private:
    void nextQuestion();
    void playInterval(int firstPitch, int secondPitch);
    void submit(int semitones);
    void setAnswerByPlaying(bool on);
    void listenForPlayedAnswer();

    IntervalTrainer trainer;
    std::string title;
    std::string progressPath;
    std::string saveError;

    IntervalQuestion question{};
    bool answered = false;     // showing feedback for `question`
    bool lastCorrect = false;
    int lastAnswer = 0;
    double answeredAt = 0.0;   // when, to move on by itself after a right answer
    int newlyUnlocked = 0;     // semitones of an interval just unlocked, shown until the next answer

    // Answering by playing: the two notes the player plays, from any starting note
    std::string inputDevice;
    bool byPlaying = false;
    std::string inputError;
    double listenFrom = 0.0;    // ignore input before this (GetTime seconds): the question's own notes are sounding
    std::vector<int> playedPitches;
    double firstPlayedAt = 0.0;
    std::string playedText;     // "E3 -> G#3", shown as the player plays

    int sessionAsked = 0;
    int sessionCorrect = 0;
    bool leave = false;
};
