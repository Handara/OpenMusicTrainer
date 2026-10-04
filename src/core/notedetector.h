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
    float onsetRiseDb = 4.5f;     // a level jump this big above the recent minimum starts a note (a soft pluck on a
                                  // string still ringing, a staccato note: 4 to 7 dB on a real bass)
};

struct DetectedNote {
    long long sample; // position in the stream (samples fed so far) where the note started
    int pitch;        // nearest MIDI pitch
    float cents;      // how far from that pitch, -50 to +50
    bool legato = false; // reached without a pluck (a hammer-on, a pull-off, a slide), from the note ringing before
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
    float envelope = 0.0f;       // the signal's level: jumps up at once, falls back slowly
    float envelopeRelease = 0.0f; // how much of the envelope is kept each sample while it falls

    std::vector<float> recentPower; // the mean square of the last few hops, oldest first
    float beforeOnsetPower = 0.0f;  // the sound's power just before the latest onset...
    double sinceOnsetPower = 0.0;   // ...and since it (summed, over sinceOnsetHops hops): a pluck makes it louder
    int sinceOnsetHops = 0;
    bool soundingBeforeOnset = false;
    bool pitchPending = false;   // an onset happened, its pitch isn't known yet
    long long onsetSample = 0;
    long long lastOnsetSample = -1000000;
    bool sounding = false;       // a note is ringing (its pitch is known)
    float liveMidi = -1.0f;      // its pitch as last measured while it rings (a bend moves it), -1 when nothing rings
    int currentPitch = -1;
    float currentMidi = -1.0f;   // its pitch exactly, as first measured: a legato change must move well away from it
    int candidatePitch = -1;     // a different pitch seen while ringing (a possible legato change)...
    int candidateCount = 0;      // ...and in how many analyses in a row
    long long candidateSample = 0;
    // A legato change confirmed, held back a moment: a pluck soon after means it was the fretting hand getting ready
    // for that pluck (a bass player frets the next note while this one rings), and only the pluck counts
    bool legatoHeld = false;
    long long legatoSample = 0;
    float legatoMidi = 0.0f;
    long long legatoDue = 0;     // when it's let out, if no pluck came
    int hopsSinceAnalysis = 0;
    int analysisLag = 0;         // the longest period looked for now (see expectLowestFrequency)
    // Every attack heard (its sample), the moment it's heard: before its pitch is known, for reacting at once (a
    // flash on the pluck; rhythm mode, where any note counts). The caller takes them and clears the list.
    std::vector<long long> attacks;
    // Every change of pitch with no attack (its sample), the moment it's believed, whether it turns out a note or the
    // hand getting ready for a pluck: where one note followed another (core/polyphony). Taken and cleared by the caller.
    std::vector<long long> changes;
};

void initNoteDetector(NoteDetector& detector, int sampleRate, const NoteDetectorConfig& config);

// The lowest note that can come now, from what a song has due: pitches are looked for only down to it, so they're
// known in two of its periods instead of two of the lowest the instrument has (a bass: 54 ms for E1, 18 ms for an A2
// due). 0 goes back to anything down to the configured minimum. A note played lower than the hint reads an octave or
// more up, never as nothing: it was a wrong note anyway.
void expectLowestFrequency(NoteDetector& detector, float frequency);

// Feeds the next samples of the stream, in any size of chunk. Notes that started are appended to `out`.
void feedNoteDetector(NoteDetector& detector, const float* samples, int count, std::vector<DetectedNote>& out);

// Whether something heard may still turn out a note: a pluck whose pitch isn't known yet, a change of pitch being
// confirmed, or one held back to see whether a pluck follows. Its sample (where the note would start), -1 for none.
long long noteDetectorPending(const NoteDetector& detector);
// A change of pitch confirmed but held back (a pluck may follow): true, with where it started and its pitch. Whoever
// knows more (a song: a slide from a pluck that matched nothing) may take it as a note at once.
bool noteDetectorHeldChange(const NoteDetector& detector, long long& sample, int& pitch);
