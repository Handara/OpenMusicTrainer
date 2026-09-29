#pragma once

#include "core/pitch.h"
#include "core/quiz.h"
#include "core/singing.h"
#include "learn/exercise.h"

#include <random>
#include <string>
#include <vector>

// Sing it back: a note plays, the player sings it and holds it in tune. A meter shows how far off the voice is as
// it sings. Progress (answers, best streak) is saved after every answer.
class SingingExercise : public Exercise {
public:
    SingingExercise(const std::string& title, const SingingConfig& config, const std::string& progressPath,
                    const std::string& inputDevice, int channel);
    ~SingingExercise() override;

    void update() override;
    void draw() override;
    bool wantsToLeave() const override { return leave; }
    int lessonScore() const override { return streak; } // right answers in a row

private:
    void nextNote();
    void playNote();
    void answer(bool right);

    std::string title;
    SingingConfig config;
    std::string progressPath;
    QuizProgress progress;
    int streak = 0;
    std::mt19937 rng;

    int target = -1;
    PitchHold hold;
    bool answered = false;
    bool lastRight = false;
    double answeredAt = 0.0;
    double listenFrom = 0.0;     // GetTime: the note's own sound is over by then

    // Listening, like the tuner: a sliding window of the input and the pitch in it
    PitchDetector detector;
    std::vector<float> window, incoming;
    float levelDb = -100.0f;
    float sungMidi = -1.0f;      // the pitch sung now, -1 for none
    int channel = -1;            // the input the voice comes in on
    std::string inputError;
    std::string saveError;
    int sessionAsked = 0, sessionCorrect = 0;
    bool leave = false;
};
