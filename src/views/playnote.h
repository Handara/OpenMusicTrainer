#pragma once

// What the play screen's views share. A view only draws: judging and timing live in gameplay.

const float HIT_FLASH_DURATION = 0.2f; // seconds a hit note stays lit

// A note as the views see it: when it's due, where it's played, and how it went
struct PlayNote {
    float time;              // song time in seconds when the note reaches the hit line
    int stringIndex;         // 0 = lowest string
    int fret;
    int pitch;               // sounding MIDI pitch: the string's tuning + fret
    float hitFlash = 0.0f;   // seconds left of the "hit" animation, 0 = none
    bool judged = false;     // hit or missed already
    bool wasPerfect = false; // the hit that started hitFlash was perfect
};

// Turns song time into x. Every view uses the same axis, so stacked views stay lined up note for note.
struct TimeAxis {
    float songTime;  // now
    float hitLineX;  // where "now" is drawn
    float noteSpeed; // pixels per second
    float xAt(float time) const { return hitLineX + (time - songTime) * noteSpeed; }
    float timeAt(float x) const { return songTime + (x - hitLineX) / noteSpeed; }
};
