#pragma once

#include "core/score.h"
#include "raylib.h"
#include "views/playnote.h"

#include <vector>

// The note highway: one lane per string, fret numbers on the notes, faint bar lines. It scrolls across (lanes stacked,
// time running past the axis's hit line) or falls (strings side by side as columns, notes coming down to a hit line
// near the bottom). Fits itself into `area`. `notes` must be sorted by time.
// `lowStringFirst`: the lowest string's lane at the top when scrolling across, on the left when falling.
void drawHighway(Rectangle area, const std::vector<PlayNote>& notes, const Score& score, const std::vector<int>& tuning,
                 bool lowStringFirst, bool falls, const TimeAxis& axis);
