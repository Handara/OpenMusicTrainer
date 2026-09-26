#pragma once

#include "core/judge.h" // PlayNote: the notes views draw

// What the play screen's views share. A view only draws: judging lives in core/judge, timing in gameplay.

// Turns song time into x. Every view uses the same axis, so stacked views stay lined up note for note.
struct TimeAxis {
    float songTime;  // now
    float hitLineX;  // where "now" is drawn
    float noteSpeed; // pixels per second
    float xAt(float time) const { return hitLineX + (time - songTime) * noteSpeed; }
    float timeAt(float x) const { return songTime + (x - hitLineX) / noteSpeed; }
};
