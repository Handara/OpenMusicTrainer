#pragma once

// Notes from the computer keyboard, for the drills: a letter (A to G) is that note, Shift raises it to its sharp, Ctrl
// lowers it to its flat. No octave: a drill takes it in the octave it's asking for (core/music nearestPitchOfClass),
// so reading and naming notes can be practiced without the instrument.

// The note a key was pressed for this frame, as a pitch class (0 = C), or -1 (none, or while typing in a text field)
int keyboardNoteClass();

const char* const KEYBOARD_NOTES_HINT = "Keyboard: A to G play the notes, Shift for a sharp, Ctrl for a flat";
