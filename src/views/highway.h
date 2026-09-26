#pragma once

#include "raylib.h"
#include "views/playnote.h"

#include <vector>

// The scrolling note highway: one lane per string, fret numbers on the notes. Fits itself into `area`.
// `notes` must be sorted by time.
void drawHighway(Rectangle area, const std::vector<PlayNote>& notes, const std::vector<int>& tuning,
                 bool lowStringOnTop, const TimeAxis& axis);
