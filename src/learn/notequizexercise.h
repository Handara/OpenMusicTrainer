#pragma once

#include "core/chart.h"
#include "core/notation.h"
#include "core/notequiz.h"
#include "core/score.h"
#include "core/settings.h"
#include "learn/exercise.h"
#include "ui/fretboardview.h"
#include "ui/menulist.h"
#include "ui/pianoboard.h"
#include "views/playnote.h"

#include <random>
#include <string>
#include <vector>

// Play this note (core/notequiz): a few notes asked one at a time, no clock, on play mode's neck. A note is asked by
// where it's played (the card lit: "Play the open high E string"), by its name, written on the staff, or by ear (it
// plays, the notes it could be outlined on the neck, Space to hear it again); played
// right, the next; played wrong, what it was and where the right one is. A run of them passes with enough right the
// first time, and the crowd cheers. The smallest step there is, for someone who's never played. On a piano (config
// piano), the keyboard instead of the neck: the key asked lit, the keys held lit too, played on a MIDI keyboard or
// the computer's (each key's letter on it).
class NoteQuizExercise : public Exercise {
public:
    NoteQuizExercise(const std::string& title, const NoteQuizConfig& config, const KeySignature& key, bool onBass,
                     const std::string& progressPath, const Settings& settings);
    ~NoteQuizExercise() override;
    void update() override;
    void draw() override;
    bool wantsToLeave() const override { return leave; } // Esc (learnBack) ends it, or the end menu's Back
    int lessonScore() const override { return passedNow; } // runs passed, this time
    bool takeFinishedRun(int& percent) override;
    bool scoresRuns() const override { return true; }
    // A run over: how it went and a menu (again, the course's next drill, back), the instrument steering it
    bool isMenu() const override { return finished; }
    bool hasEndMenu() const override { return true; }
    void offerNext(const std::string& label) override { nextLabel = label; }
    bool takeNextChosen() override;

private:
    void startRun();
    void playPrompt();      // by ear: the reference, then the note asked
    void played(int pitch, bool heard); // `heard`: from the instrument (not the keyboard or a click)
    void finish();
    std::string promptText() const;
    void drawProgress(float left, float right, float top, float s);
    void drawStaffPrompts(float left, float top, float width, float height);
    void drawNeck(float left, float right, float top, float bottom, float s);
    void drawEnd(float top, float s);
    int drawKeys(float left, float right, float top, float bottom, float s); // the key clicked, -1 for none
    void sound(int pitch, float seconds, double at); // on the instrument's own sound, at a time on the engine's clock

    std::string title;
    NoteQuizConfig config;
    KeySignature key;
    bool onBass;
    std::string progressPath;
    Settings settings;
    NoteQuizStats stats;
    NoteQuizRun run;
    std::mt19937 random;
    bool finished = false, passed = false;
    int passedNow = 0;
    int finishedPercent = -1; // a run just ended, its score: until it's taken
    double rightAt = -100.0, wrongAt = -100.0, finishedAt = -100.0;
    double runStartedAt = 0.0;
    double playPromptAt = -1.0;  // by ear: when to play the note asked (GetTime), -1 for not to
    double soundingUntil = -1.0; //   while it sounds, what's heard is hardthz's own (a microphone hears the speakers)
    double heardAt = -100.0;     //   when the note asked started sounding: a ring pulses with it
    NeckStep lastRight{ -1, -1, -1 }; // the note just played right: its ring
    // The prompts written down, for the staff: a bar of four at a time, the one now lit
    Chart chart;
    Score score;
    std::vector<PlayNote> staffNotes;
    float shownTime = 0.0f; // eases to the note now: the page turns smoothly
    std::vector<float> cheer;
    FretboardLayout board;  // as last drawn: for clicks
    PianoBoard keys;        //   on a piano
    bool listening = false;
    std::string inputError;
    MenuList endMenu;
    std::string nextLabel;    // the course's next drill, offered on the end menu ("" for none)
    bool nextChosen = false;
    bool leave = false;
};
