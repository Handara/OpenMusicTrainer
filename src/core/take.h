#pragma once

#include "core/chart.h"
#include "core/positions.h"

#include <vector>

// A take: a part played in on its instrument over the song, written down as it's played. Each note goes on the
// grid's nearest step (the editor's own: sixteenths, triplets...), on the string and fret nearest where the hand
// was, and is held until the next note is played or until its sound has died away. Strings plucked together are
// written together. What comes out is what was played, mistakes and all: the editor is where it's put right, and
// the whole take is one step to undo. Pure logic: whoever hears the instrument says what was played and when.

const double TAKE_SAME_PLUCK_S = 0.06; // notes this close in time were plucked together
const float TAKE_SILENCE_DB = -50.0f;  // quieter than this, nothing is ringing
const float TAKE_DIES_DB = 24.0f;      // a note has ended when the sound is this far under its loudest

struct Take {
    struct Written {
        int tick, stringIndex; // the note in the track
        double seconds;        // when it was plucked, in the song
        bool ringing;          // its end isn't known yet
    };
    std::vector<Written> written; // every note this take has put in the track, in the order played
    StringFret hand{-1, -1};      // where the last note was played: the next is looked for near it
    float loudestDb = -120.0f;    // the loudest the sound has been since the last pluck
};

// Notes plucked at `seconds` in the song (the chart's time, as tickToSeconds gives it): one, or several together.
// They're written into the track, which stays sorted; a note already on that step of that string is replaced.
// Several notes for a pluck already written as one (they're known a moment later: core/polyphony) take its place.
// Notes still ringing end here. A pitch the part's neck doesn't have is left out. Plucks come in the order played.
void takePluck(Take& take, const Chart& chart, FrettedTrack& track, int step, const std::vector<int>& pitches, double seconds);

// How loud the instrument is at `seconds`: the notes still ringing are held to here, and end once the sound has
// died away. Called as the take goes, every frame or so.
void takeLevel(Take& take, const Chart& chart, FrettedTrack& track, int step, float levelDb, double seconds);

// The take is over at `seconds`: what's still ringing ends there
void endTake(Take& take, const Chart& chart, FrettedTrack& track, int step, double seconds);
