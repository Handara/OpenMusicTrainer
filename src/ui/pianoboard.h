#pragma once

#include "imgui.h"
#include "ui/pianoview.h"

#include <functional>

// A piano keyboard for the drills, in the place a guitar's play mode neck has: whole octaves from a C, covering the
// notes asked (three octaves at least, the notes towards the middle), as wide as given. Each key's look comes from the drill (the key to play lit,
// the ones held, right or wrong); the Cs say which they are, and middle C has a dot.
struct PianoBoard {
    ImVec2 origin;
    float whiteWidth = 0.0f, height = 0.0f;
    int firstPitch = 48; // a C
    int keys = 25;
    ImVec4 keyRect(int pitch) const;      // x, y, width, height; a pitch off the board: all zero
    bool shows(int pitch) const { return pitch >= firstPitch && pitch < firstPitch + keys; }
};

PianoBoard pianoBoard(float left, float top, float width, float maxHeight, int lowPitch, int highPitch);
// Returns the pitch clicked, -1 for none
int drawPianoBoard(const PianoBoard& board, float scale, const std::function<PianoKeyStyle(int pitch)>& style);
