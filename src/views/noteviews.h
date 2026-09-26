#pragma once

#include "core/settings.h"
#include "raylib.h"
#include "views/playnote.h"

#include <vector>

// Every view the settings turn on, stacked in `area` top to bottom (sheet music, then the highway), on one
// time axis so they line up note for note. The one place that decides where each view goes.
void drawNoteViews(Rectangle area, const NoteViews& views, const std::vector<PlayNote>& notes, const std::vector<float>& barTimes,
                   const std::vector<int>& tuning, bool lowStringOnTop, const TimeAxis& axis);
