#pragma once

#include <memory>

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

    // What a lesson's goal counts, for exercises a lesson can include: a drill's clean passes this time, an interval
    // exercise's right answers in a row. 0 for the rest.
    virtual int lessonScore() const { return 0; }
};

struct ExerciseEntry;
// Makes the exercise an entry describes. The learn screen owns that knowledge (it's the one place that knows
// every type); routines and lessons are handed the function, so they can start exercises without knowing their types.
using ExerciseFactory = std::unique_ptr<Exercise> (*)(const ExerciseEntry& entry);
