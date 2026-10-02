#pragma once

#include <vector>

// Where a note is played on a fretted instrument. The same pitch usually lives in several places (the E above
// middle C: the open high E, the B string's 5th fret, the G's 9th, the D's 14th), and the pitch alone can't say
// which was used. Where the hand is can: players rarely leap across the neck, so the likeliest place is the one
// nearest the last note's.

struct StringFret {
    int string; // 0 = lowest
    int fret;
    bool operator==(const StringFret& other) const { return string == other.string && fret == other.fret; }
};

// Every place the pitch is found, frets 0 to maxFret, lowest string first
std::vector<StringFret> positionsOf(int pitch, const std::vector<int>& tuning, int maxFret);

// The likeliest of those places: the nearest to where the hand last was (by fret, then by string), or with no last
// place (string -1), the lowest fret. Nothing to choose from gives {-1, -1}.
StringFret likeliestPosition(const std::vector<StringFret>& places, StringFret last);

// Where notes played together sit: each on a string of its own, the first where likeliestPosition puts it and the
// others as near it as their strings allow (a chord is one hand shape). In the pitches' order; {-1, -1} for a note
// that's off the neck, or has no string left.
std::vector<StringFret> chordPositions(const std::vector<int>& pitches, const std::vector<int>& tuning, int maxFret, StringFret last);

// The part of the neck a song needs, for showing it whole: from the nut when the notes stay low (players find their
// way from it), else from a fret below the lowest; to a fret past the highest, and at least `minFrets` wide
struct FretSpan {
    int first; // 0: the open strings and the nut are shown
    int last;
};
FretSpan fretSpanFor(const std::vector<int>& frets, int minFrets, int maxFret);
