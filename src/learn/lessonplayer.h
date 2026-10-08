#pragma once

#include "core/lessondoc.h"
#include "learn/exercise.h"
#include "learn/lessonpage.h"
#include "screens/gameplay.h"

#include <map>
#include <memory>
#include <string>
#include <vector>

// A lesson, a page at a time (learn/lessonpage draws it): Back and Next (the arrows, the buttons at the foot), dots
// for the pages. Pages set aside (help) are skipped, shown only when a block missed twice sends the student there;
// Next then goes back. A block aced the first time may send the student on ahead. Practice blocks make their drill
// as they start, from the notes the student misses most (or the lesson's own). A page's drills and songs (its scored blocks) are cards: one chosen (Up and Down) or clicked runs
// over the whole screen, with a bar at the top saying its goal; once it's met, the lesson comes back (or, on a page
// with nothing else, goes on). A block that gates holds Next back until it's passed; Enter starts the first one still
// to pass, else turns the page. Progress is saved as it goes: the lesson reopens where the student was.
class LessonPlayer : public Exercise {
public:
    // `exercises`: what each exercise block runs, by its key (core/lessondoc scoredBlockKey): a copy
    // `playOptions`: how song blocks play (the player's gameplay settings)
    // `startPage`: where it opens (-1: where the student was)
    // `songFolders`: where the game's songs are, for song blocks that name one
    LessonPlayer(const LessonEntry& entry, std::map<int, ExerciseEntry> exercises, ExerciseFactory create,
                 const GameplayOptions& playOptions, std::vector<std::string> songFolders, const std::string& progressPath,
                 int startPage = -1);
    ~LessonPlayer() override;

    void update() override;
    void draw() override;
    bool wantsToLeave() const override { return leave; }
    bool back() override; // a block running: back to its page
    bool isMenu() const override { return !running || running->isMenu(); }

private:
    std::vector<BlockPlace> pageBlocks() const; // the page's scored blocks, in reading order
    bool passed(const BlockPlace& place) const;
    bool canGoOn() const;          // every block on the page that gates is passed
    int firstToPass() const;       // the first block still holding the page, -1 for none
    const ExerciseEntry* exerciseFor(const BlockPlace& place) const;
    int goal(const BlockPlace& place) const;
    void goTo(int page);
    void goOn();                   // the next page, or the lesson finished
    void start(int number);        // runs the page's scored block
    void stopRunning();
    void sendTo(int page);         // to a page a block names (help, or ahead), coming back from help
    int nextPage(int from, int by) const; // the next page that isn't set aside, that way; -1 for none
    void save();
    void drawFoot(float s);        // the dots, Back and Next
    void drawGoalBar(float s);     // over a running block

    LessonDoc doc;
    std::string folder;
    std::map<int, ExerciseEntry> exercises;
    std::map<int, ExerciseEntry> made;       // practice blocks' drills, made as they started (by key)
    std::map<int, int> misses, runs;         // each block's runs missed in a row, and runs played, this time
    ExerciseFactory create;
    GameplayOptions playOptions;
    std::vector<std::string> songFolders;
    std::string progressPath;
    LessonProgress progress;
    std::string saveError;

    int page = 0;
    float scroll = 0.0f, pageHeight = 0.0f;
    int chosen = -1;                  // the page's scored block chosen with the keys
    bool chosenMoved = false;         //   just now: scrolled into view as it's drawn
    PageMedia media;
    std::unique_ptr<Exercise> running;
    BlockPlace runningPlace;
    double passedAt = -1.0;           // when the running block's goal was met: back a moment later
    int helpFrom = -1;                // the page help was shown for: Next goes back to it
    int sendPage = -1;                // a page a block sends the student to, a moment after its run
    double sendAt = 0.0;
    bool leave = false;
};
