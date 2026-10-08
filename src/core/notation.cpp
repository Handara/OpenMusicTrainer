#include "core/notation.h"

// The seven letters C D E F G A B (0 to 6) and the pitch class of each without sharps or flats
const int NATURAL_PITCH_CLASS[7] = { 0, 2, 4, 5, 7, 9, 11 };
// The order sharps and flats are added to key signatures, as letters: F C G D A E B, and B E A D G C F
const int SHARP_ORDER[7] = { 3, 0, 4, 1, 5, 2, 6 };
const int FLAT_ORDER[7]  = { 6, 2, 5, 1, 4, 0, 3 };
// Their places on the treble staff: F5 C5 G5 D5 A4 E5 B4, and B4 E5 A4 D5 G4 C5 F4
const int SHARP_POSITIONS[7] = { 8, 5, 9, 6, 3, 7, 4 };
const int FLAT_POSITIONS[7]  = { 4, 7, 3, 6, 2, 5, 1 };

// The bottom line as a count of letter steps from C-1 (MIDI octave -1): E4 in treble (octave 4, letter E), G2 in bass
const int TREBLE_BOTTOM_LINE_STEP = (4 + 1) * 7 + 2;
const int BASS_BOTTOM_LINE_STEP = (2 + 1) * 7 + 4;
// A bass clef's key signature is the treble clef's, a third (two steps) lower: F#5 on the top line becomes F#3 on
// the fourth line
const int BASS_KEY_SIGNATURE_SHIFT = -2;

int keyAlteration(const KeySignature& key, int letter){
    for (int i = 0; i < key.fifths; i++) if (SHARP_ORDER[i] == letter) return 1;
    for (int i = 0; i < -key.fifths; i++) if (FLAT_ORDER[i] == letter) return -1;
    return 0;
}

int keySignaturePosition(const KeySignature& key, int index, Clef clef){
    int position = key.fifths >= 0 ? SHARP_POSITIONS[index] : FLAT_POSITIONS[index];
    return clef == Clef::Bass ? position + BASS_KEY_SIGNATURE_SHIFT : position;
}

StaffNote staffNote(int writtenPitch, const KeySignature& key, Clef clef){
    int pitchClass = ((writtenPitch % 12) + 12) % 12;
    // Which letter, with which alteration: the key's own spelling first, then a plain natural, then a sharp or flat
    int letter = -1, alteration = 0;
    for (int l = 0; l < 7 && letter < 0; l++){
        int a = keyAlteration(key, l);
        if ((NATURAL_PITCH_CLASS[l] + a + 12) % 12 == pitchClass){ letter = l; alteration = a; }
    }
    for (int l = 0; l < 7 && letter < 0; l++) if (NATURAL_PITCH_CLASS[l] == pitchClass){ letter = l; alteration = 0; }
    int sharpOrFlat = key.fifths >= 0 ? 1 : -1;
    for (int l = 0; l < 7 && letter < 0; l++){
        if ((NATURAL_PITCH_CLASS[l] + sharpOrFlat + 12) % 12 == pitchClass){ letter = l; alteration = sharpOrFlat; }
    }
    // The letter's octave: B#3 sounds as C4 and Cb4 as B3, so it comes from the pitch minus the alteration
    int natural = writtenPitch - alteration;
    int octave = (natural - NATURAL_PITCH_CLASS[letter]) / 12; // MIDI octaves counted from C-1 = 0
    int step = octave * 7 + letter;                            // letter steps: 7 per octave
    return { step - (clef == Clef::Bass ? BASS_BOTTOM_LINE_STEP : TREBLE_BOTTOM_LINE_STEP), alteration, letter };
}

Accidental accidentalFor(const StaffNote& note, const KeySignature& key, BarAccidentals& bar){
    auto written = bar.alterations.find(note.position);
    int inForce = written != bar.alterations.end() ? written->second : keyAlteration(key, note.letter);
    if (note.alteration == inForce) return Accidental::None;
    bar.alterations[note.position] = note.alteration;
    if (note.alteration > 0) return Accidental::Sharp;
    if (note.alteration < 0) return Accidental::Flat;
    return Accidental::Natural;
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

int writtenPitchAt(int position, const KeySignature& key, Clef clef){
    for (int pitch = 0; pitch < 128; pitch++){
        const StaffNote note = staffNote(pitch, key, clef);
        if (note.position == position && note.alteration == keyAlteration(key, note.letter)) return pitch;
    }
    return -1;
}
