#pragma once

#include <string>

// Where notes go on a staff. Pure math: drawing is the view's job.

// Guitar music is written an octave higher than it sounds (a treble clef with a small 8 below it),
// so the staff isn't covered in ledger lines: the open low E (E2) is written as E3.
const int GUITAR_WRITTEN_OCTAVE_SHIFT = 12;

struct StaffNote {
    int position;   // steps from the bottom line: 0 = bottom line (E4 in treble), 1 = the space above, 8 = top line;
                    // negative = below the staff, above 8 = above it. Each step is one letter name.
    int accidental; // 0 = natural, +1 = sharp (flats come with key signatures)
};

const int STAFF_TOP_LINE = 8;
const int STAFF_MIDDLE_LINE = 4;

// Treble clef position of a written MIDI pitch. Black keys are spelled as sharps for now.
StaffNote trebleStaffNote(int writtenPitch);

// Ledger lines: the short extra lines a note outside the staff needs, counted from the staff outward
int ledgerLineCount(int position);

// Notes on or above the middle line get their stem pointing down, lower ones up, so stems stay on the staff
bool stemUp(int position);

// A key signature, as its place on the circle of fifths: the number of sharps (1 to 7) or flats (-1 to -7).
// G major and E minor are 1 (F#), F major is -1 (Bb), C major and A minor are 0.
struct KeySignature {
    int fifths = 0;
    bool minor = false;
};

// "G" + "major", "F#" + "minor", "Bb" + "major"... Each letter has its place on the circle of fifths (F -1, C 0,
// G 1 ... B 5), a sharp moves 7 places up, a flat 7 down, and a minor key sits 3 below its major.
// False for keys with more than 7 sharps or flats (G# major: write Ab major).
bool parseKeySignature(const std::string& tonic, const std::string& mode, KeySignature& out);
std::string keySignatureName(const KeySignature& key); // "G major", "Eb major", "F# minor"
