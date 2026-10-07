#pragma once

#include <vector>

// Checking an instrument is in tune: before a song, the player plays each open string and hardthz says how far off it
// is; while the song is played, hardthz watches the notes for the instrument going out of tune. Pure logic; the
// tuning check screen and the play screen feed it what they hear.

// Which open string a heard pitch (a fractional MIDI note) is: the string whose note is nearest, an octave off
// counting as its own (low strings are often heard an octave up). -1 if none is within `range` semitones.
int openStringHeard(float midi, const std::vector<int>& tuning, float range = 2.0f);
// How far a pitch is off a note, in cents (+ sharp, - flat), the octave aside: -600 to +600
float centsOff(float midi, int note);

// The check before a song: each string in tune once it's been heard within IN_TUNE_CENTS for a moment
const float IN_TUNE_CENTS = 10.0f;
const float IN_TUNE_HOLD_S = 0.35f; // a pluck's first moment runs sharp: it must settle in tune, not pass through
struct StringCheck {
    bool heard = false;
    float cents = 0.0f;      // as last heard
    float inTuneFor = 0.0f;  // seconds heard in tune without a break
    bool tuned = false;      // stays true once reached
};
struct TuningCheck {
    std::vector<int> tuning;
    std::vector<StringCheck> strings;
    int lastString = -1;     // the string heard last, -1 before any
};
TuningCheck startTuningCheck(const std::vector<int>& tuning);
// A pitch heard for `seconds` (a frame's worth); midi <= 0 for nothing heard
void hearForTuning(TuningCheck& check, float midi, float seconds);
bool allTuned(const TuningCheck& check);

// While playing: every note heard near a note that was due gives how far off that note it was. Notes a bend, a
// slide or a slip puts off go one way or another; an instrument out of tune puts most of them off the same way. So
// the latest notes are looked at together: most of them off, the same way, by more than OUT_OF_TUNE_CENTS.
const int TUNING_WATCH_NOTES = 8;       // looked at together
const float OUT_OF_TUNE_CENTS = 30.0f;  // a clear third of a half step: heard as sour, and past the pluck's own sharpness
const float TUNING_WATCH_RANGE = 75.0f; // further off, it's another note (a slip), not the same one out of tune
struct TuningWatch {
    std::vector<float> offsets; // cents, the latest last
};
// A note heard `offsetCents` from the nearest note due then; ignored past TUNING_WATCH_RANGE
void watchTuning(TuningWatch& watch, float offsetCents);
// True when the latest notes say the instrument is out of tune; `cents` is by how much (+ sharp)
bool looksOutOfTune(const TuningWatch& watch, float& cents);
