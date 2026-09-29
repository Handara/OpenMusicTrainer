#pragma once

#include "core/score.h"
#include "core/settings.h"
#include "raylib.h"
#include "views/playnote.h"

#include <vector>

// Every view the settings turn on, stacked in `area` top to bottom (sheet music, tab, highway), on one
// time axis so they line up note for note. The one place that decides where each view goes. The hit line moves
// right if the sheet music's clef, key and time signature need the room, in every view at once. A falling highway
// gets a column of its own: on the right beside the others, or centered when it's the only view.
// `notes` are the track's notes in the score's order.
// Returns where the hit line ended up (x), for anything drawn at it.
float drawNoteViews(Rectangle area, const NoteViews& views, const std::vector<PlayNote>& notes, const Score& score,
                   const std::vector<int>& tuning, bool lowStringOnTop, TimeAxis axis);
