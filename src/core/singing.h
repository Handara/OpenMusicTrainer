#pragma once

#include <random>

// Singing a note back: the game plays one, the player sings it and holds it in tune. The logic only (which notes,
// when a sung note counts); listening and drawing are the exercise's job.

struct SingingConfig {
    int lowest = 48;              // the range the notes are picked from (MIDI): C3...
    int highest = 67;             // ...to G4, where most voices, high and low, can reach at least an octave
    bool naturalsOnly = true;     // only C D E F G A B; otherwise the sharps and flats too
    bool anyOctave = true;        // the right note in any octave counts: a low voice answering a high note an
                                  // octave down has matched it
    float toleranceCents = 30.0f; // this close counts as in tune
    double holdSeconds = 1.0;     // held in tune this long counts as sung
};

// A note in the range, never the one just asked (when there's a choice)
int nextSingingNote(const SingingConfig& config, std::mt19937& rng, int last);

// How far a sung pitch (fractional MIDI) is from the target, in cents: -50 to +50 in any octave when anyOctave is on
// (the nearest octave of the target), otherwise the plain distance
float singingErrorCents(const SingingConfig& config, int target, float sungMidi);

// Holding a note: fed each moment's reading, it counts the time spent in tune without a break
struct PitchHold {
    double inTuneFor = 0.0;
};
// A reading (fractional MIDI pitch; below 0 for no clear pitch) lasting `seconds`. Returns true once held long
// enough. Out of tune or silent, the count starts over.
bool holdPitch(PitchHold& hold, const SingingConfig& config, int target, float sungMidi, double seconds);
