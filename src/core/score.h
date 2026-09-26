#pragma once

#include "core/chart.h"
#include "core/notation.h"

#include <vector>

// The engraver: turns a track's notes, which are only ticks, into music as it's written down. Note values, dots,
// rests, ties, triplets and beams are worked out from the ticks and the time signatures, never stored in the chart,
// so they can't disagree with the timing. Each event also gets its time in seconds, for the scrolling views.
//
// How long a note is written: its length if the chart gives one (a sustain), else until the next note, but no
// further than its bar line (so a note before a long gap shows rests, not a chain of ties).
//
// Which values: the longest that fits, where
// - a note that starts off the beat can't cross into the next beat (it's split and tied, so beats stay visible);
// - a note longer than a beat starts on a beat (in 6/8, 9/8, 12/8 the beat is a dotted quarter, and a note
//   longer than one is a whole number of beats);
// - a rest is at most a beat long, or exactly half the bar from its start or middle (a half rest on beat 1 or 3
//   of 4/4, never in 3/4), and only compound meters dot them;
// - a beat whose notes fall on thirds (or sixths) of it is a triplet beat.
// Eighths and shorter are beamed together within each beat (each dotted quarter in compound meters).

enum class NoteValue { Whole, Half, Quarter, Eighth, Sixteenth, ThirtySecond };

struct ScoreEvent {
    int tick;
    int length;        // in ticks: what's played, a triplet eighth is a third of a beat
    float time;        // in seconds, from the chart's tempo map
    NoteValue value;   // as written: a triplet eighth is written as an eighth
    int dots;          // 0 or 1
    int tuplet;        // 0, or 3 for a triplet: three of the written value in the time of two
    bool rest;
    bool wholeBarRest; // a bar with nothing in it: one whole rest in the middle, whatever the time signature
    bool tiedToNext;   // the note carries on into the next event (across a beat or a bar line)
    int firstNote;     // the chord: indices into the track's notes (the next noteCount of them); -1 for rests
    int noteCount;
    int beamGroup;     // events with the same number are beamed together; -1 = not beamed (a lone eighth gets a flag)
    int bar;           // index into Score::bars
};

struct ScoreBar {
    int tick;
    float time;
    TimeSignatureChange timeSignature;
    KeySignature key;
    bool showTimeSignature; // the first bar, and wherever it changes
    bool showKey;
};

struct Score {
    std::vector<ScoreEvent> events; // in time order
    std::vector<ScoreBar> bars;     // every bar line up to the chart's end: the last may only close the music
};

Score buildScore(const Chart& chart, const FrettedTrack& track);

int noteValueTicks(NoteValue value, int resolution); // undotted, untupled: a quarter is `resolution`
int beamCount(NoteValue value);                      // 1 for an eighth, 2 for a sixteenth, 3 for a 32nd
