#pragma once

#include "core/score.h"
#include "raylib.h"
#include "views/playnote.h"

#include <string>
#include <vector>

// Tablature that scrolls like the other views: one line per string and each note as its fret number on its
// string. The highest string is on top, the way tab is always printed, whatever the highway's string order.
// Rhythm (stems under the numbers) comes with note values in the chart format.

// A text font for the fret numbers; without it, raylib's built-in pixel font is used
bool loadTabFont(const std::string& path);
void unloadTabFont();

// `notes` must be sorted by time; bar lines come from the score
void drawTab(Rectangle area, const std::vector<PlayNote>& notes, const Score& score, int stringCount, const TimeAxis& axis);
