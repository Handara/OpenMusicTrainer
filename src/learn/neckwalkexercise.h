#pragma once

#include "core/groove.h"
#include "core/neckwalk.h"
#include "core/settings.h"
#include "learn/exercise.h"
#include "ui/fretboardview.h"

#include <string>
#include <vector>

// Neck walk, a game (core/neckwalk): a note a round, walked on two or three strings over a tune in its key
// (core/groove), with a drum kit and a crowd that cheers a walk all right and goes "awww" at one that isn't. Play
// mode's neck shows where the note is and lights the next place to the beat; a strip along the top shows the phrase:
// the bar to listen, the walk's beats, the crowd's. Five rounds wrong and it's over; each game is kept.
class NeckWalkExercise : public Exercise {
public:
    NeckWalkExercise(const std::string& title, const std::string& tunePath, bool onBass, const std::string& progressPath,
                     const Settings& settings);
    ~NeckWalkExercise() override;
    void update() override;
    void draw() override;
    bool wantsToLeave() const override { return false; } // Esc (learnBack) ends it
    int lessonScore() const override { return bestCleared; } // the most rounds cleared in a game, this time

private:
    void start();
    void finish();
    double gameTime() const; // the tune's clock, less the output's delay (the global offset): what's heard now
    void scheduleTune();     // the tune's bars about to play, and the crowd once a round's walk is all judged
    void handle(const NeckWalkEvents& events);
    void played(int pitch, double time);
    void drawStrip(float left, float right, float top, float s);
    void drawNeck(float left, float right, float top, float bottom, float s);
    void drawResults(float left, float top, float width, float s);

    std::string title;
    bool onBass;
    std::vector<int> tuning;
    std::string progressPath;
    Settings settings;
    Groove groove;
    std::string grooveError;
    NeckWalkStats stats;
    NeckWalkGame game;
    enum class State { Ready, Playing, Over } state = State::Ready;
    int nextBar = 0;        // the tune's next bar to schedule, counted from the game's start
    int crowdRound = -1;    // the round whose crowd is scheduled
    std::vector<float> kit[5], cheer, aww; // rendered once
    // What happened when (GetTime), for the drawing
    std::vector<double> judgedAt; // each walk note of the round
    double verdictAt = -100.0, roundAt = -100.0, overAt = -100.0;
    bool lastCheer = false;
    bool newBest = false;
    int bestCleared = 0;
    FretboardLayout board;  // as last drawn: for clicks
    bool listening = false;
    std::string inputError;
};
