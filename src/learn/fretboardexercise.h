#pragma once

#include "core/fretboard.h"
#include "learn/exercise.h"

#include <string>
#include <vector>

// Find a note on the fretboard: "C on the A string". Click its fret, or play it. Progress (answers, best streak) is
// saved after every answer.
class FretboardExercise : public Exercise {
public:
    FretboardExercise(const std::string& title, const FretboardConfig& config, const std::string& progressPath,
                      const std::string& inputDevice, int channel);
    ~FretboardExercise() override;

    void update() override;
    void draw() override;
    bool wantsToLeave() const override { return leave; }
    int lessonScore() const override { return trainer.streak; } // right answers in a row

private:
    void nextQuestion();
    void answer(bool right, int fret);
    void setAnswerByPlaying(bool on);
    void drawFretboard(float left, float top, float width, float scale);

    FretboardTrainer trainer;
    std::string title;
    std::string progressPath;
    std::string saveError;

    FretboardQuestion question{};
    bool answered = false;
    bool lastRight = false;
    int clickedFret = -1;       // the fret clicked, -1 when answered by playing
    int playedPitch = -1;       // the note heard, -1 when answered by clicking
    double answeredAt = 0.0;

    std::string inputDevice;
    int channel = -1;          // the input the instrument is plugged into
    bool byPlaying = false;
    std::string inputError;
    double listenFrom = 0.0;    // ignore notes before this (GetTime seconds): the last answer's sound may still ring

    int sessionAsked = 0;
    int sessionCorrect = 0;
    bool leave = false;
};
