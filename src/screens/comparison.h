#pragma once

#include "imgui.h"
#include "screens/gameplay.h"

// The end of a song, note by note: what was written and what was played, on one roll of pitches against time.
// Written notes are bars on their pitch's row, lit by how they went (green perfect, accent good, red outline missed);
// every note heard from the instrument is a dot on its row where it was played, green where a written note of that
// pitch was there to be played, red where none was (a wrong note). Scrolls along the song with the wheel, the arrows
// or a drag.
void drawNoteComparison(const GameResult& result, ImVec2 min, ImVec2 max, float s);
void resetNoteComparison(); // back to the song's first notes, for the next run's
