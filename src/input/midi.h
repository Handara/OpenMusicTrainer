#pragma once

#include "input/noteinput.h"

#include <string>
#include <vector>

// Notes from a MIDI device: a keyboard, a pad controller, electronic drums. On Linux through the kernel's raw MIDI
// devices (/dev/snd/midi*), on Windows through the system's winmm; no library needed. A thread reads the device
// and stamps each message as it arrives, so notes are timed to the millisecond, not to the frame.

std::vector<std::string> midiDeviceNames(); // asks the system: not every frame
// Opens a device by name (empty: the first there is). False, with the reason, if there's none or it can't be opened.
bool startMidiInput(const std::string& device, std::string& error);
void stopMidiInput(); // safe to call more than once
bool midiInputActive();
const char* midiDeviceName(); // the device open, "" for none

// The notes played since the last call, each with how long ago it started; call once per frame
const std::vector<PlayedNote>& updateMidiInput();
// Which notes are held down right now (by pitch, 0 to 127): for showing them on a keyboard
const bool* midiKeysDown();
