#pragma once

#include "raylib.h"
#include "views/playnote.h"

#include <string>
#include <vector>

// Keys: notes fall onto a piano keyboard at the bottom, each above the key that plays it, as long as it's held.
// The keyboard covers the part's notes in whole octaves (two at least). Keys held down on the player's keyboard
// light up; a hit note bursts on its key and is gone, a missed one goes by as a ghost. `notes` must be sorted.
// keysDown: 128 flags by MIDI pitch (nullptr for none).
// keyLabel: the computer key that plays a pitch ("Z"), printed on its key; nullptr for none
void drawPianoHighway(Rectangle area, const std::vector<PlayNote>& notes, const TimeAxis& axis, const bool* keysDown,
                      std::string (*keyLabel)(int pitch) = nullptr);
