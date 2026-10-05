#pragma once

#include "core/chords.h"
#include "core/settings.h"
#include "learn/exercise.h"

#include <string>
#include <vector>

// Chord changes: the chords come in turn with the metronome; strum each one on its first beat. With the instrument,
// a change counts when the strum is on time and sounds like the chord; with the keyboard, any number key is a strum
// and only the timing counts. Faster after each clean pass; the best clean tempo is saved.
class ChordExercise : public Exercise {
public:
    ChordExercise(const std::string& title, const ChordDrillConfig& config, const std::string& progressPath,
                  const Settings& settings);
    ~ChordExercise() override;

    void update() override;
    void draw() override;
    bool wantsToLeave() const override { return leave; }
    int lessonScore() const override { return cleanPassesNow; }

private:
    struct Change {
        double time;       // audio time of the chord's first beat
        int chord;         // index into config.chords
        bool judged = false;
        bool hit = false;
    };
    enum class Heard { Nothing, Right, WrongChord, Late, Missed };

    void startPass();
    void finishPass();
    double drillTime() const;
    void strummed(double time, long long sample); // sample: where the strum starts in the input, -1 for a key
    void checkPendingChord();

    std::string title;
    ChordDrillConfig config;
    std::string progressPath;
    Settings settings;
    DrillProgress progress;

    bool running = false;
    int tempo = 0;
    double countInStart = 0.0;
    double passEndTime = 0.0;
    int nextClick = 0, totalClicks = 0;
    std::vector<Change> changes;
    int hits = 0;
    int cleanPassesNow = 0;
    std::string passText;
    Heard lastHeard = Heard::Nothing;
    double lastHeardAt = -100.0;   // when, for its flash (GetTime)
    std::string lastHeardChord;

    // Listening: strums found in the raw input, and the sound after the latest one, to check its chord
    StrumDetector strums;
    std::vector<float> history;      // the latest input samples, up to a second
    long long historyEnd = 0;        // the input position just after history's last sample
    long long pendingStart = -1;     // a strum waiting for enough sound to be checked, from this sample
    int pendingChange = -1;          // the change it's on time for
    std::string inputError;
    bool leave = false;
};
