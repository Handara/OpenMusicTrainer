#pragma once

#include "raylib.h"
#include "views/playnote.h"

#include <vector>

// The neck: osu!'s circles on an instrument's fretboard. The part of the neck the song needs is drawn once, and each
// note appears on its string and fret with a ring around it closing in: when the ring meets the note, play it. The
// next notes are joined by a faint line, so the hand's path through a phrase can be read ahead. A hit bursts like
// the highway's, green for perfect; a miss fades where it was. A long note is an osu! slider: its length is a track
// on its rim (one loop a whole note, a quarter note a quarter of it) which the ring lands on, and which a ball eats
// clockwise as the note rings. The time a ring takes to close follows the note speed. `notes` must be sorted by time.
// `wholeNeck`: the neck from the nut to the instrument's last fret (20 on a bass, 22 on a guitar, more if the song
// goes higher); else only the part the song needs.
void drawNeckView(Rectangle area, const std::vector<PlayNote>& notes, const std::vector<int>& tuning, bool lowStringOnTop,
                  bool wholeNeck, const TimeAxis& axis);

// Where a note is on the neck as it was last drawn (its middle, and its radius), for showing its judgement over it.
// False when the neck isn't being drawn.
bool neckNoteAt(const PlayNote& note, float& x, float& y, float& radius);
