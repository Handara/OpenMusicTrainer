#pragma once

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
};
