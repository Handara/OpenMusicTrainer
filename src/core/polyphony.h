#pragma once

#include "core/notedetector.h"

#include <vector>

// Several notes at once, heard without being told which to look for: a bass's double stop, two or three strings of a
// guitar plucked together. The note detector (core/notedetector) follows one note at a time: two together reach it
// as one of them, as a note that's neither, or as nothing. core/chords' soundHoldsNotes checks for the notes a song
// has due; this finds them unasked, for where nothing is due: the instrument screen, a part being recorded.
//
// A note is a row of peaks in the sound's spectrum, its harmonics: at 1, 2, 3... times its frequency. The note whose
// row accounts for the most of what's there is taken first, its peaks are set aside, and what's left is looked at
// again for another note. A note an octave above another has every one of its peaks on one of the lower note's, so
// nothing is left of it: it shows only in the lower note's even harmonics being far stronger than its odd ones.
//
// It needs NOTES_LISTEN_S of the notes ringing together, so its answer comes that long after the pluck (the pluck's
// own time is kept). Notes that close together in time aren't told apart from notes played together. Pure math.

const float NOTES_LISTEN_S = 0.2f;

struct HeardPitch {
    int pitch;      // MIDI
    float strength; // its harmonics' amplitudes added up: for comparing one sound with another
};

// The notes a sound holds between two pitches (an instrument's lowest and highest), lowest first; empty for silence
std::vector<HeardPitch> notesInSound(const float* samples, int count, int sampleRate, int lowestPitch, int highestPitch,
                                     int maxNotes = 3);

// Plucks in a stream that held several notes. It's fed the same samples as a note detector, and what that found in
// them: its attacks say where the plucks are, and its notes that came with no attack (a hammer-on, a slide) say where
// one note followed another instead of sounding with it.
struct PluckNotes {
    long long sample;         // the pluck, in the stream
    std::vector<int> pitches; // two or more, lowest first
};

struct PluckListener {
    int sampleRate = 0;
    int lowestPitch = 0, highestPitch = 0;
    int listen = 0;                  // samples listened to after a pluck, and compared with as many before it
    std::vector<float> recent;       // the latest samples, oldest first
    long long position = 0;          // samples fed so far
    std::vector<long long> plucks;   // heard, and waiting for enough sound after them
    std::vector<long long> attacks;  // the latest attacks, waiting or not: a note found on one isn't a legato one
    std::vector<long long> changes;  // where notes started with no attack
};

void initPluckListener(PluckListener& listener, int sampleRate, int lowestPitch, int highestPitch);

// Feeds the next samples of the stream, with the attacks and notes the note detector found in them (or before: a
// note's pitch is known a moment after its attack). Plucks that held two notes or more are appended to `out`, each
// once NOTES_LISTEN_S of sound has come after it. Only notes that started with the pluck count: one still ringing
// from before isn't played again.
void feedPluckListener(PluckListener& listener, const float* samples, int count, const std::vector<long long>& attacks,
                       const std::vector<DetectedNote>& notes, std::vector<PluckNotes>& out);
