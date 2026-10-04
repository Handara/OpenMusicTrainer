#pragma once

#include "imgui.h"

#include <vector>

// A fretted instrument's neck, drawn with ImGui: strings across (the highest on top, as in tab), frets left to
// right, the open strings in a column left of the nut. The fret exercise and the Instrument screen draw on it.

struct FretboardLayout {
    float left = 0, top = 0, width = 0, height = 0; // the board, open column included; fret numbers go below it
    float scale = 1;
    int strings = 6;
    int firstFret = 0, lastFret = 12;  // 0 as the first fret: the open column is shown
    float boardLeft = 0;               // the nut, or the first shown fret's wire
    float fretWidth = 0;
    float spacing = 0;                 // between strings

    float stringY(int string) const;   // string 0 is the lowest, at the bottom
    float fretX(int fret) const;       // the middle of a fret's column: frets are pressed just behind the wire
    int fretAt(float x) const;         // the fret shown under x, -1 for none
    int stringAt(float y) const;       // the string nearest y, -1 when between none
};

// `spacing`: between strings, in pixels at the scale; 0 for the usual (30)
FretboardLayout fretboardLayout(float left, float top, float width, float scale, int strings, int firstFret, int lastFret,
                                float spacing = 0.0f);

// The board, its markers, frets and numbers, and the strings with their names left of it. `lit`: a string drawn in
// brass (-1 for none).
void drawFretboard(const FretboardLayout& layout, const std::vector<int>& tuning, int lit = -1);

// A round mark on a string's fret, with a name on it (may be empty)
void drawFretDot(const FretboardLayout& layout, int string, int fret, float radius, ImU32 fill, ImU32 ink, const char* name);
