#include "core/notation.h"

// For each of the 12 pitch classes (C, C#, D, ...): its letter (C = 0 ... B = 6) and whether it needs a sharp
const int LETTER_OF_PITCH_CLASS[12] = { 0, 0, 1, 1, 2, 3, 3, 4, 4, 5, 5, 6 };
const int SHARP_OF_PITCH_CLASS[12]  = { 0, 1, 0, 1, 0, 0, 1, 0, 1, 0, 1, 0 };

// E4, the treble staff's bottom line, as a count of letter steps from C-1 (MIDI octave -1): octave 4, letter E
const int TREBLE_BOTTOM_LINE_STEP = (4 + 1) * 7 + 2;

StaffNote trebleStaffNote(int writtenPitch){
    int pitchClass = ((writtenPitch % 12) + 12) % 12;
    int octave = (writtenPitch - pitchClass) / 12;          // MIDI octaves counted from C-1 = 0
    int step = octave * 7 + LETTER_OF_PITCH_CLASS[pitchClass]; // letter steps: 7 per octave, whatever the sharps
    return { step - TREBLE_BOTTOM_LINE_STEP, SHARP_OF_PITCH_CLASS[pitchClass] };
}

int ledgerLineCount(int position){
    if (position < 0) return -position / 2;                // -2 and -3 need one line (at -2), -4 needs two...
    if (position > STAFF_TOP_LINE) return (position - STAFF_TOP_LINE) / 2;
    return 0;
}

bool stemUp(int position){
    return position < STAFF_MIDDLE_LINE;
}

// The letters F C G D A E B, in circle of fifths order, from -1 to 5
const char* const FIFTHS_LETTERS = "FCGDAEB";

bool parseKeySignature(const std::string& tonic, const std::string& mode, KeySignature& out){
    if (tonic.empty() || tonic.size() > 2) return false;
    const char* letter = nullptr;
    for (const char* l = FIFTHS_LETTERS; *l; l++) if (*l == tonic[0]) letter = l;
    if (!letter) return false;
    int fifths = (int)(letter - FIFTHS_LETTERS) - 1;
    if (tonic.size() == 2){
        if (tonic[1] == '#') fifths += 7;
        else if (tonic[1] == 'b') fifths -= 7;
        else return false;
    }
    bool minor;
    if (mode == "major") minor = false;
    else if (mode == "minor") minor = true;
    else return false;
    if (minor) fifths -= 3;
    if (fifths < -7 || fifths > 7) return false;
    out = { fifths, minor };
    return true;
}

std::string keySignatureName(const KeySignature& key){
    int tonic = key.fifths + (key.minor ? 3 : 0) + 1; // index into FIFTHS_LETTERS, plus 7 per sharp, minus 7 per flat
    std::string name(1, FIFTHS_LETTERS[((tonic % 7) + 7) % 7]);
    if (tonic >= 7) name += '#';
    if (tonic < 0) name += 'b';
    return name + (key.minor ? " minor" : " major");
}
