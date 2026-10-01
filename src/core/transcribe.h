#pragma once

#include "core/chart.h"

#include <string>
#include <vector>

// Writing a bass part down from a recording of it alone: a stem split from a song, or the player's own playing.
// A bass plays one note at a time, which lahn's note detector hears well; the beat is found from the notes' attacks,
// following a band that speeds up or slows down; the notes are put on that beat's sixteenths, and given strings and
// frets a hand reaches easily. What comes out is a draft: the song editor is where it's put right. Pure logic.

struct HeardNote {
    double start, end; // seconds into the recording
    int pitch;         // MIDI
};

// Every note in a recording of a bass alone (mono, any level: it's evened out first)
std::vector<HeardNote> hearNotes(const std::vector<float>& samples, int sampleRate);

// The beat under some notes: when each beat falls (seconds), steady or drifting. Empty when there's too little to go on.
std::vector<double> findBeats(const std::vector<HeardNote>& notes, double length);

struct Transcription {
    Chart chart;           // a bass part on the beats found; its audio isn't set
    double bpm = 0.0;      // the tempo, on average
    int notes = 0;
};

// The chart: its tempo following the beats, tick 0 on a downbeat (the chart's offset is where that falls in the
// recording), 4/4, the notes on sixteenths, on strings and frets
bool transcribeBass(const std::vector<float>& samples, int sampleRate, const std::string& title, Transcription& out,
                    std::string& error);
