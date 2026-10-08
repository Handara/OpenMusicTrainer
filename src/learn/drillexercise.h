#pragma once

#include "audio/band.h"
#include "core/journal.h"
#include "core/drill.h"
#include "core/judge.h"
#include "core/score.h"
#include "core/settings.h"
#include "learn/exercise.h"
#include "ui/menulist.h"

#include <functional>
#include <string>
#include <vector>

// What a drill plays, and how
struct DrillSetup {
    std::string about;          // under the title: "Natural minor in E", "Quarters, eighths and rests"
    DrillTempo tempo;
    std::vector<int> tuning;
    KeySignature key;
    int beatsPerBar = 4;        // x/4
    bool timingOnly = false;    // any number key or played note counts: only when it's played is judged (rhythm)
    bool staffOnly = false;     // sheet music only, whatever the settings show (sight reading: no tab to read instead)
    bool showWhere = false;     // a neck too, between the text and the notes, the next note lit on it (a help to read)
    std::function<std::vector<DrillNote>()> nextPass; // the notes of each pass: a scale's are the same every time,
                                                      // a rhythm's new
};

// A drill: notes scroll by in time with a metronome, a pass at a time, faster each time they're played cleanly.
// Judged from the number keys or the player's instrument; the best clean tempo is saved. It waits on the first pass's
// notes until Space (or the instrument's choose, its open G string: input/menuinput), and after each pass shows how
// it went with a menu: again (at the tempo it earned), faster or the same, the course's next drill, back. Waiting and
// on that menu, the instrument steers as in the menus. It's played with a backing band (audio/band): drums, bass and
// keys in the drill's own style, chords fitting the notes read; or (B) to the metronome alone.
class DrillExercise : public Exercise {
public:
    DrillExercise(const std::string& title, const DrillSetup& setup, const std::string& progressPath, const Settings& settings);
    ~DrillExercise() override;

    void update() override;
    void draw() override;
    bool wantsToLeave() const override { return leave; }
    int lessonScore() const override { return cleanPassesNow; }
    bool takeFinishedRun(int& percent) override;
    bool scoresRuns() const override { return true; }
    bool goesOn() const override { return true; } // each clean pass, faster
    bool isMenu() const override { return stage != Stage::Running; }
    bool hasEndMenu() const override { return true; }
    void offerNext(const std::string& label) override { nextLabel = label; }
    bool takeNextChosen() override;

private:
    enum class Stage { Waiting, Running, Ended };
    void startPass(bool fresh); // fresh: new notes from the setup (not for the first pass: it plays what's shown)
    void stopPass();            // Space while playing: back to waiting on the same notes
    void placePass(double downbeat);
    void finishPass();
    double drillTime() const; // the audio clock, minus the output offset: what the notes are timed against
    void drawWhere(float left, float right, float top, float bottom, float s); // the neck, the next note lit
    void drawEnd(float s);      // how the pass went, and the menu after it
    void drawHistory(float left, float top, float width, float height, float s); // the last passes' tempos, a line
    void drawCountIn(float s);  // the count-in's beats, big: 4, 3, 2, 1
    void drawCombo(float s);    // the notes in a row, while it's 3 or more
    void toggleBand();          // the band, or the metronome alone (kept for every drill)

    std::string title;
    DrillSetup setup;
    std::string progressPath;
    Settings settings;
    DrillProgress progress;
    std::vector<DrillNote> drillNotes; // this pass, in beats
    Chart chart;                       // the same pass as a chart, timed for this pass

    Stage stage = Stage::Waiting;  // Space starts and stops; a pass played to its end stops on the end menu
    int tempo = 0;                 // of the current pass
    double countInStart = 0.0;     // audio time of the first count-in click
    double firstNoteTime = 0.0;    // audio time of the pass's first bar's first beat
    double passEndTime = 0.0;      // when the pass is over and judged
    int nextClick = 0;             // the next metronome click to schedule, counted from countInStart
    int totalClicks = 0;
    std::vector<PlayNote> notes;   // the pass's notes in audio time, for judging and the views
    Score score;                   // the pass written down, in audio time

    int hits = 0;
    int combo = 0, bestCombo = 0;  // notes in a row played right, this pass (a miss breaks it), and its most
    double comboAt = -100.0;       //   when it last grew (GetTime): it pops
    bool burstDue = false;         // a note just hit: sparks from where it's shown (the neck), drawn next
    int burstCombo = 0;            // the run of notes the last milestone's sparks were for
    int missedSoFar = 0;           // the notes gone by unplayed: one more breaks the combo
    int cleanPassesNow = 0;        // clean passes since the drill was opened (a lesson's goal counts these)
    int finishedPercent = -1;      // a pass just ended, its share of notes hit: until it's taken
    std::string passText;          // the last pass's result
    std::string inputError;
    int lastPlayedPitch = -1;
    PlayNote lastHit{ 0.0f, -1, -1, -1 }; // the last note hit, and when: ringed on the neck a moment
    double hitAt = -100.0;
    bool leave = false;
    // The end menu: the pass just played, and what's offered after it
    MenuList endMenu;
    int endTempo = 0, endHits = 0, endTotal = 0;
    DrillPassOutcome endOutcome{ false, 0, false };
    double endedAt = -100.0;
    int endCombo = 0;              // the pass's best run of notes in a row
    float endMeanMs = 0.0f, endSpreadMs = 0.0f; // its timing: how early on average (negative: late), give or take
    int endTimed = 0;              //   of how many notes hit
    std::vector<Activity> history; // this drill's last passes, the one just played last (app/playerprogress)
    std::string nextLabel;         // the course's next drill, offered there ("" for none)
    bool nextChosen = false;
    // The backing band: its style (the drill's own, from its id), each pass's song
    BandPlayer band;
    bool bandOn = true;
    std::string bandPath;          // where the choice of band or metronome is kept
    unsigned bandSeed = 0;
    int bandStyle = 0;
    int passNumber = 0;            // each pass's song its own
    BandSong song;
};
