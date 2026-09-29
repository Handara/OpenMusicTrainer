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
// channel: which of the device's inputs to listen to (from 0), -1 for all of them mixed
bool startNoteInput(const std::string& inputDevice, float minFrequency, std::string& error, int channel = -1);
void stopNoteInput(); // safe to call more than once
bool noteInputActive();

// Reads everything the input delivered since the last call; returns the notes that started in it. Call once per frame.
const std::vector<PlayedNote>& updateNoteInput();
// The attacks heard in that update, how long ago each (seconds), the moment they're heard: a note's pitch comes a
// little later (core/notedetector). For reacting at once, and for rhythm mode, where any note counts.
const std::vector<double>& noteInputAttacks();
// The lowest note a song has due now (Hz), so pitches are known sooner; 0 for anything (core/notedetector)
void expectLowestNote(float frequency);
float noteInputLevelDb(); // loudness of the latest input, for a level meter
// The raw samples the last updateNoteInput read, oldest first: for exercises that listen to more than single notes
// (chords). At noteInputSampleRate.
const std::vector<float>& latestInputSamples();
int noteInputSampleRate();
