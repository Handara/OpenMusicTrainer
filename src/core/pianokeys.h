#pragma once

#include <string>
#include <vector>

// Playing piano on the computer keyboard: which key plays which note. The layout virtual pianos use, so it
// feels familiar: the bottom row plays an octave (Z X C V B N M white, S D G H J black between them), the top row
// the one above (Q W E R T Y U I O P, and 2 3 5 6 7 9 0). 29 notes, C up to E two octaves higher. Keys are named
// as the settings file writes them ("Z", "2", ",") and can be changed; "none" leaves a note without one.

const int PIANO_KEY_SLOTS = 29; // notes from a C: C D E... up to the E an octave and a third above the next C

std::vector<std::string> defaultPianoKeys();

// The C the layout starts on for a part whose lowest note is this: the C at or below it
int pianoBaseFor(int lowestPitch);

// Gives a note a key; if another note had that key, it loses it (one key plays one note)
void bindPianoKey(std::vector<std::string>& keys, int slot, const std::string& key);
