#pragma once

#include "input/noteinput.h"

#include <string>
#include <vector>

// The computer keyboard as a piano (the layout and its rules: core/pianokeys). Up and Down move it an octave.

// Starts listening, the layout's first key on `base` (a C)
void startPianoKeys(const std::vector<std::string>& keys, int base);
void stopPianoKeys();
bool pianoKeysActive();

// The notes pressed since the last call (age 0: they're read once a frame); call once per frame
const std::vector<PlayedNote>& updatePianoKeys();
const bool* pianoKeysDown();   // 128 flags by pitch: the notes held down now
std::string pianoKeyFor(int pitch); // the key that plays this pitch now ("Z"), "" if none does

// Key names, as the settings write them, to raylib's keys and back (0 / "" for none)
int pianoKeyCode(const std::string& name);
std::string pianoKeyName(int code);
