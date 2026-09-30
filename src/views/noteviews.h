#pragma once

#include "core/score.h"
#include "core/settings.h"
#include "raylib.h"
#include "views/playnote.h"

#include <vector>

// Every view the settings turn on, stacked in `area` top to bottom (the sheet music, the neck). The one place that
// decides where each view goes.
// `notes` are the track's notes in the score's order.
// A keys part: the sheet music on top (if the settings show it), and the piano, notes falling onto its keys, under
// it. keysDown: 128 flags by pitch, the keys held down now (nullptr for none). Returns the x the judgements go at.
// keyLabel: the computer key that plays a pitch, printed on its piano key (nullptr for none)
float drawKeysViews(Rectangle area, const NoteViews& views, const std::vector<PlayNote>& notes, const Score& score,
                    TimeAxis axis, const bool* keysDown, std::string (*keyLabel)(int pitch) = nullptr);

// Returns where the hit line ended up (x), for anything drawn at it.
float drawNoteViews(Rectangle area, const NoteViews& views, const std::vector<PlayNote>& notes, const Score& score,
                   const std::vector<int>& tuning, bool lowStringOnTop, TimeAxis axis);
