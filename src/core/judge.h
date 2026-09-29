#pragma once

#include <vector>

// Judging: matching what the player did to the notes they were meant to play, and how well-timed it was.
// Shared by gameplay and learn-mode drills, whatever the input (keyboard or a real instrument).

const double PERFECT_WINDOW_S = 0.040; // an input this close to a note's time is perfect...
const double NEAR_WINDOW_S = 0.100;    // ...this close still counts; later than this after it, the note is missed
const float HIT_FLASH_DURATION = 0.2f; // seconds a hit note stays lit on screen

// A note being played: when it's due, where it's played, and how it went. Views draw these.
struct PlayNote {
    float time;              // song time in seconds when the note reaches the hit line
    int stringIndex;         // 0 = lowest string
    int fret;
    int pitch;               // sounding MIDI pitch: the string's tuning + fret
    float length = 0.0f;     // seconds it's held (0 for a note that isn't): views draw it that long
    float beats = 0.0f;      // its written length in beats (quarter notes), as the sheet music has it, ties added up
    float writtenLength = 0.0f; // the same in seconds: how long it should ring
    float hitFlash = 0.0f;   // seconds left of the "hit" animation, 0 = none
    bool judged = false;     // hit or missed already
    bool hit = false;        // judged and hit (judged without hit = missed)
    bool wasPerfect = false;
};

// Something the player did. A keyboard knows which string's key was pressed but not the pitch;
// an instrument's note detector knows the pitch but not the string. Unknown fields are -1.
struct PlayerInput {
    double time;          // song time of the input
    int stringIndex = -1;
    int pitch = -1;
    // A single-note detector hears one note of a chord, so by pitch a chord counts whole once one of its notes
    // is heard. A MIDI keyboard sends every key, so there each note of a chord has to be played: false.
    bool completesChord = true;
    bool anyNote = false; // rhythm mode, played on an instrument: whatever note is played counts for the next hit
};

enum class Judgement { Ignored, Perfect, Near };

struct JudgeResult {
    Judgement judgement = Judgement::Ignored;
    int notesHit = 0;    // more than 1 when a detected pitch completes a chord (see judgeInput)
    double error = 0.0;  // note time - input time: positive = early, negative = late
    int pitch = -1;      // the note hit, as it sounds (to play it back to someone playing on the keyboard)
    int noteIndex = -1;  // which note (a chord's first), for showing the judgement where it is
};

// Judges one input against the nearest unjudged note it matches, within the near window. An input with
// nothing close enough is ignored (a stray press). `notes` must be sorted by time.
// A single-note detector hears one note of a chord, so matching a chord note by pitch counts the whole chord.
JudgeResult judgeInput(std::vector<PlayNote>& notes, const PlayerInput& input);

// Marks unjudged notes more than the near window before `now` as missed; returns how many
// latest: the index of the latest note missed, for showing where (untouched when none is)
int markMisses(std::vector<PlayNote>& notes, double now, int* latest = nullptr);
