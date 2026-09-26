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
