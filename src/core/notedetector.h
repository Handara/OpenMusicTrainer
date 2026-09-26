#pragma once

#include "core/pitch.h"

#include <vector>

// Turns a stream of samples from an instrument or voice into note events: "this pitch started at this sample".
// Pure math, fed by whoever has the samples (the microphone input in the game, generated audio in tests).
//
// Onset: a note starts when the level jumps well above its recent minimum (a pluck, a strum, a re-pluck).
// Pitch: measured with YIN once enough sound has arrived after the attack, but the note is stamped with its
// onset, so timing judgments don't depend on how long the pitch took to find.
// Legato: a pitch change without a new attack (hammer-on, pull-off, slide) that holds steady is a new note too.

struct NoteDetectorConfig {
    float minFrequency = 70.0f;   // lowest note expected; guitar's low E is 82 Hz, a bass needs about 30
    float maxFrequency = 1400.0f; // highest; the 24th fret of the high e is 1319 Hz
    float silenceDb = -50.0f;     // quieter than this is silence
    float onsetRiseDb = 6.0f;     // a level jump this big above the recent minimum starts a note
};

struct DetectedNote {
    long long sample; // position in the stream (samples fed so far) where the note started
    int pitch;        // nearest MIDI pitch
    float cents;      // how far from that pitch, -50 to +50
};

struct NoteDetector {
    NoteDetectorConfig config;
    int sampleRate = 0;
    int hopSize = 0;             // samples analyzed together (about 2.7 ms)
    PitchDetector pitch;
    std::vector<float> window;   // the latest samples, as many as YIN needs, oldest first
    std::vector<float> hop;      // samples waiting to make a full hop
    std::vector<float> recentDb; // levels of the last few hops, to spot a sudden rise
    long long position = 0;      // samples fed so far

    bool pitchPending = false;   // an onset happened, its pitch isn't known yet
    long long onsetSample = 0;
    long long lastOnsetSample = -1000000;
    bool sounding = false;       // a note is ringing (its pitch is known)
    int currentPitch = -1;
    int candidatePitch = -1;     // a different pitch seen while ringing (a possible legato change)...
    int candidateCount = 0;      // ...and in how many analyses in a row
    long long candidateSample = 0;
    int hopsSinceAnalysis = 0;
};

void initNoteDetector(NoteDetector& detector, int sampleRate, const NoteDetectorConfig& config);

// Feeds the next samples of the stream, in any size of chunk. Notes that started are appended to `out`.
void feedNoteDetector(NoteDetector& detector, const float* samples, int count, std::vector<DetectedNote>& out);
