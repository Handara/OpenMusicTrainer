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
// A keys part: the sheet music on top (if the settings show it), and the piano, notes falling onto its keys, under
// it. keysDown: 128 flags by pitch, the keys held down now (nullptr for none). Returns the x the judgements go at.
float drawKeysViews(Rectangle area, const NoteViews& views, const std::vector<PlayNote>& notes, const Score& score,
                    TimeAxis axis, const bool* keysDown);

// Returns where the hit line ended up (x), for anything drawn at it.
float drawNoteViews(Rectangle area, const NoteViews& views, const std::vector<PlayNote>& notes, const Score& score,
                   const std::vector<int>& tuning, bool lowStringOnTop, TimeAxis axis);
