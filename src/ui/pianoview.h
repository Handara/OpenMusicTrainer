#pragma once

#include "imgui.h"

#include <functional>
#include <string>

// A piano's keys, drawn with ImGui: the settings' key chooser and the Instrument screen draw on it. Keys are
// numbered from 0, the first key drawn, which is a C.

struct PianoKeyStyle {
    ImU32 fill = 0;     // 0: the key's own white or black
    std::string label;  // printed at the bottom of the key
    ImU32 ink = 0;      // the label's color; 0: what reads on the fill
};

int pianoWhiteKeys(int count); // how many of the first `count` keys are white
bool pianoKeyIsBlack(int key);

// Draws `count` keys from `origin`, each white key `whiteWidth` wide; `style` says how each key looks (hovered: the
// mouse is on it). Returns the key under the mouse, -1 for none.
int drawPianoKeys(ImVec2 origin, float whiteWidth, float height, int count,
                  const std::function<PianoKeyStyle(int key, bool hovered)>& style);
