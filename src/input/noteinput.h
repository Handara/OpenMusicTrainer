#pragma once

#include <string>
#include <vector>

// Notes played on a real instrument (or sung), from the input device: the microphone feeding the note detector.

struct PlayedNote {
    int pitch;   // nearest MIDI pitch
    float cents; // how far off it, -50 to +50
    double age;  // seconds from the note's start to the newest sample received. A screen places it in time as
                 // (its clock now) - age - input offset; the offset covers the device's own delay (see calibration).
};

// minFrequency: the lowest note expected (guitar about 70 Hz, bass about 30 Hz); lower costs a little detection time
bool startNoteInput(const std::string& inputDevice, float minFrequency, std::string& error);
void stopNoteInput(); // safe to call more than once
bool noteInputActive();

// Reads everything the input delivered since the last call; returns the notes that started in it. Call once per frame.
const std::vector<PlayedNote>& updateNoteInput();
float noteInputLevelDb(); // loudness of the latest input, for a level meter
