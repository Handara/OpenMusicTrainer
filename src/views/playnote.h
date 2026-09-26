#pragma once

#include "core/judge.h" // PlayNote: the notes views draw
#include "raylib.h"

// What the play screen's views share. A view only draws: judging lives in core/judge, timing in gameplay.

// Turns song time into x. Every view uses the same axis, so stacked views stay lined up note for note.
struct TimeAxis {
    float songTime;  // now
    float hitLineX;  // where "now" is drawn
    float noteSpeed; // pixels per second
    float xAt(float time) const { return hitLineX + (time - songTime) * noteSpeed; }
    float timeAt(float x) const { return songTime + (x - hitLineX) / noteSpeed; }
};

// The printed look, shared by the sheet music and the tab so they read as one score when stacked
const Color PAPER = { 242, 232, 212, 255 };
const Color INK = { 34, 26, 22, 255 };
const Color HIT_LINE = { 200, 150, 40, 255 };
const Color PERFECT_COLOR = { 40, 170, 80, 255 };
const Color NEAR_COLOR = { 210, 150, 20, 255 };
