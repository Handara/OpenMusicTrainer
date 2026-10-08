#pragma once

#include <map>
#include <string>

// Where notes go on a staff. Pure math: drawing is the view's job.

// Guitar and bass music are written an octave higher than they sound (guitar in a treble clef with a small 8 below
// it, bass in a bass clef), so the staff isn't covered in ledger lines: the guitar's open low E (E2) is written as E3.
const int WRITTEN_OCTAVE_SHIFT = 12;

// Treble: guitar, bottom line E4. Bass: bass guitar, bottom line G2.
enum class Clef { Treble, Bass };

struct StaffNote {
    int position;   // steps from the bottom line: 0 = bottom line (E4 in treble, G2 in bass), 1 = the space above,
                    // 8 = top line; negative = below the staff, above 8 = above it. Each step is one letter name.
    int alteration; // how the note is spelled on that letter: +1 sharp, -1 flat, 0 natural. What the note *is*:
                    // whether a symbol is drawn depends on the key and the bar (see accidentalFor)
    int letter;     // the letter itself: 0 = C ... 6 = B
};

const int STAFF_TOP_LINE = 8;
const int STAFF_MIDDLE_LINE = 4;

// A key signature, as its place on the circle of fifths: the number of sharps (1 to 7) or flats (-1 to -7).
// G major and E minor are 1 (F#), F major is -1 (Bb), C major and A minor are 0.
struct KeySignature {
    int fifths = 0;
    bool minor = false;
};

// "G" + "major", "F#" + "minor", "Bb" + "major"... Each letter has its place on the circle of fifths (F -1, C 0,
// G 1 ... B 5), a sharp moves 7 places up, a flat 7 down, and a minor key sits 3 below its major.
// False for keys with more than 7 sharps or flats (G# major: write Ab major).
// Staff position and spelling of a written MIDI pitch, in a key. Notes of the key are spelled the key's way
// (Bb in F major, E# in F# major); others as their natural if there is one, else sharp in sharp keys and C,
// flat in flat keys.
StaffNote staffNote(int writtenPitch, const KeySignature& key = {}, Clef clef = Clef::Treble);

// The written pitch a staff position stands for, as the key spells it (the top line in G major: F#); -1 for none
int writtenPitchAt(int position, const KeySignature& key = {}, Clef clef = Clef::Treble);
// The key's alteration of a letter (0 = C ... 6 = B): +1 for F in G major, -1 for B in F major, else 0
int keyAlteration(const KeySignature& key, int letter);
// Where the key signature's sharps or flats sit on the staff, in the order they're written (index 0 first)
int keySignaturePosition(const KeySignature& key, int index, Clef clef = Clef::Treble);

// The symbol drawn before a note
enum class Accidental { None, Sharp, Flat, Natural };

// Accidentals written so far in the current bar: once written, one holds for its staff position until the bar
// line. Use a fresh one for every bar.
struct BarAccidentals {
    std::map<int, int> alterations; // staff position -> alteration now in force there
};
// What to draw before a note, given the key and the bar so far; records it in `bar`
Accidental accidentalFor(const StaffNote& note, const KeySignature& key, BarAccidentals& bar);

// Ledger lines: the short extra lines a note outside the staff needs, counted from the staff outward
int ledgerLineCount(int position);

// Notes on or above the middle line get their stem pointing down, lower ones up, so stems stay on the staff
bool stemUp(int position);

bool parseKeySignature(const std::string& tonic, const std::string& mode, KeySignature& out);
std::string keySignatureName(const KeySignature& key); // "G major", "Eb major", "F# minor"
