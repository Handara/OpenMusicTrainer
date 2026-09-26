#pragma once

#include "raylib.h"
#include "views/playnote.h"

#include <string>
#include <vector>

// Sheet music that scrolls like the highway: notes sit on a treble staff at their time, with bar lines.
// Written the guitar way (an octave above how it sounds). Note values, beams and rests come later,
// with time and key signatures in the chart format.

// The music font (Bravura, a SMuFL font). Without it the staff still works, drawn with plain shapes.
bool loadStaffFont(const std::string& path);
void unloadStaffFont();

// `notes` must be sorted by time; `barTimes` are the song times where bars start
void drawStaff(Rectangle area, const std::vector<PlayNote>& notes, const std::vector<float>& barTimes, const TimeAxis& axis);
