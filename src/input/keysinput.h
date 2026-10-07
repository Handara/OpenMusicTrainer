#pragma once

#include "core/settings.h"
#include "input/noteinput.h"

#include <string>
#include <vector>

// A piano for the drills, as play mode has for keys parts: a MIDI keyboard when one is connected, else the computer
// keyboard laid out as one (core/pianokeys), from the C at or below the lowest note the drill asks for.
void startKeysInput(const Settings& settings, int lowestPitch);
void stopKeysInput();
const std::vector<PlayedNote>& updateKeysInput(); // the notes pressed since the last call; once a frame
const bool* keysInputDown();                      // 128 flags by pitch: held down now (nullptr when not listening)
bool keysInputIsMidi();
std::string keysInputLabel(int pitch);            // the computer key that plays it ("Z"), "" for none or a MIDI keyboard
