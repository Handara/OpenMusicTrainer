#pragma once

#include "core/notedetector.h"

#include <string>
#include <vector>

// Notes played on a real instrument (or sung), from the input device: the microphone feeding the note detector.

struct PlayedNote {
    int pitch;   // nearest MIDI pitch
    float cents; // how far off it, -50 to +50
    double age;  // seconds from the note's start to the newest sample received. A screen places it in time as
                 // (its clock now) - age - input offset; the offset covers the device's own delay (see calibration).
    bool legato = false; // reached without a pluck: a hammer-on, a pull-off, a slide, a bend (core/notedetector)
    Technique technique = Technique::Pluck; // which of them
};

// minFrequency: the lowest note expected (guitar about 70 Hz, bass about 30 Hz); lower costs a little detection time
// channel: which of the device's inputs to listen to (from 0), -1 for all of them mixed
bool startNoteInput(const std::string& inputDevice, float minFrequency, std::string& error, int channel = -1);
void stopNoteInput(); // safe to call more than once
// Counts the times note input was started: who started it can tell whether another has since
int noteInputGeneration();
// Stops listening without closing the capture: another screen opened it since, and reads it now
void releaseNoteInput();
bool noteInputActive();

// Reads everything the input delivered since the last call; returns the notes that started in it. Call once per frame.
const std::vector<PlayedNote>& updateNoteInput();
// The attacks heard in that update, how long ago each (seconds), the moment they're heard: a note's pitch comes a
// little later (core/notedetector). For reacting at once, and for rhythm mode, where any note counts.
const std::vector<double>& noteInputAttacks();
// A pluck that held several notes at once (a double stop, a chord): all of them, lowest first. The note detector
// follows one note at a time, so for such a pluck updateNoteInput gave one of them, a note that's neither, or
// nothing: whoever shows or writes down what's played puts these in that note's place (it has the same age, give or
// take a few milliseconds). They're known a moment after the pluck (core/polyphony: NOTES_LISTEN_S).
struct PlayedChord {
    std::vector<int> pitches;
    std::string name; // the chord they make, heard from the whole sound ("G", "Am7"); "" for none known
    double age; // seconds from the pluck to the newest sample received, as a note's
};
// The plucks of several notes found by the last updateNoteInput
const std::vector<PlayedChord>& noteInputChords();
// The lowest note a song has due now (Hz), so pitches are known sooner; 0 for anything (core/notedetector)
void expectLowestNote(float frequency);
float noteInputLevelDb(); // loudness of the latest input, for a level meter
// The raw samples the last updateNoteInput read, oldest first: for exercises that listen to more than single notes
// (chords). At noteInputSampleRate.
const std::vector<float>& latestInputSamples();
int noteInputSampleRate();
// Something heard that may still turn out a note (core/notedetector): how long ago it started (seconds, like a note's
// age), or a negative number for nothing. A note isn't missed while what was played on it is still being listened to.
double noteInputPendingAge();
// The pitch of the note ringing now, as last measured (a fractional MIDI number: a bend moves it); -1 for none
float noteInputLivePitch();
// A change of pitch with no pluck, held back to see whether a pluck follows (core/notedetector): its pitch and age
bool noteInputHeldChange(int& pitch, double& age);

// A check of what's heard: from now, everything the input reads and every attack, note and pluck of several notes the
// detectors find is kept (up to a few minutes), to be saved as <base>.wav (the samples, as the detector gets them)
// and <base>.txt (what was found, in seconds into the recording). For finding out why a note was misheard.
void startInputRecording();
bool inputRecording();
double inputRecordingSeconds();
bool saveInputRecording(const std::string& basePath, std::string& error); // stops it too
void cancelInputRecording();
