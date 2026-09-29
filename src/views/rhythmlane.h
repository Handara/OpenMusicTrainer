#pragma once

#include "core/score.h"
#include "raylib.h"
#include "views/playnote.h"

#include <vector>

// Rhythm mode's lane, taiko-style: hits slide in from the right to a target at the axis's hit line. A note's string
// is its kind (0 = don, brass; 1 = ka, blue) and its fret says whether it's big (1: a chord). A hit bursts at the
// target and is gone; a miss goes on as a ghost. Faint bar lines keep the measure. `notes` must be sorted.
void drawRhythmLane(Rectangle area, const std::vector<PlayNote>& notes, const Score& score, const TimeAxis& axis);
