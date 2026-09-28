#pragma once

#include "core/judge.h" // PlayNote: the notes views draw
#include "raylib.h"
#include "ui/theme.h"

// What the play screen's views share. A view only draws: judging lives in core/judge, timing in gameplay.

// Turns song time into x. Every view uses the same axis, so stacked views stay lined up note for note.
struct TimeAxis {
    float songTime;  // now
    float hitLineX;  // where "now" is drawn
    float noteSpeed; // pixels per second
    // Bar lines are drawn this far before their bar's time, so a note on the downbeat (and its sharp or flat) sits
    // just after its bar line instead of on it: everything is placed by time, with no room made for bar lines.
    // Set by the layout (views/noteviews), the same for every view so their bar lines line up.
    float barLineGap = 12.0f;
    float xAt(float time) const { return hitLineX + (time - songTime) * noteSpeed; }
    float timeAt(float x) const { return songTime + (x - hitLineX) / noteSpeed; }
};

// Colors come from the theme by role (ui/theme), so the views work in light and dark: the sheet music and the tab
// are printed in ink on the card color, the hit line is the brass accent, a hit lights up good (perfect) or brass (near).
