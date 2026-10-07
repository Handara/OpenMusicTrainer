#pragma once

#include <memory>
#include <string>

// One kind of learn-mode activity: interval ear training now, later scale drills, rhythm, sight reading...
// The learn screen runs whichever one is active through this interface, without knowing which kind it is.
// That's what `virtual` buys: each kind brings its own update and draw, and adding a new kind means
// writing a new class, not changing the learn screen.
class Exercise {
public:
    virtual ~Exercise() = default; // virtual, so deleting through an Exercise pointer runs the right destructor

    virtual void update() = 0;             // once per frame, before draw: timing, sounds, keyboard shortcuts
    virtual void draw() = 0;               // ImGui widgets, inside the learn screen's window
    virtual bool wantsToLeave() const = 0; // the player pressed Back
    // Esc (or the Back button) while it runs: true if it handled it itself (a lesson stops the step running in it,
    // back to the lesson), false to be closed
    virtual bool back(){ return false; }

    // What a lesson's goal counts, for exercises a lesson can include: a drill's clean passes this time, an interval
    // exercise's right answers in a row. 0 for the rest.
    virtual int lessonScore() const { return 0; }

    // A run (a pass, a set of questions) just ended: true once, with its score in percent, for a course's drill to
    // keep its best. False for exercises that don't score runs (the course falls back on lessonScore).
    virtual bool takeFinishedRun(int& percent){ (void)percent; return false; }
    virtual bool scoresRuns() const { return false; }
    // It goes on after a run passes, the next one harder (a timed drill, faster): a course doesn't move on by itself
    // from it, the player does
    virtual bool goesOn() const { return false; }
    // It's a page to choose from just now, not something being played (a course's chapter between its drills, a
    // drill waiting to start or showing how a run went): the instrument can move around it as in the menus
    virtual bool isMenu() const { return false; }
    // After a run it stops on a menu (again, faster, next, back) instead of going on by itself. A course offers what
    // comes next there ("Next drill: F alone"; "" for nothing), and hears when it's chosen (true once).
    virtual bool hasEndMenu() const { return false; }
    virtual void offerNext(const std::string& label){ (void)label; }
    virtual bool takeNextChosen(){ return false; }
};

struct ExerciseEntry;
// Makes the exercise an entry describes. The learn screen owns that knowledge (it's the one place that knows
// every type); routines and lessons are handed the function, so they can start exercises without knowing their types.
using ExerciseFactory = std::unique_ptr<Exercise> (*)(const ExerciseEntry& entry);
