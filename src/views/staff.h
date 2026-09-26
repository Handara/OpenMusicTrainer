#pragma once

#include "core/score.h"
#include "raylib.h"
#include "views/playnote.h"

#include <string>
#include <vector>

// Sheet music that scrolls like the highway: the engraved score (core/score) on a treble staff, each event at its
// time. Written the guitar way (an octave above how it sounds). The clef, key and time signature of the bar being
// played stay on the left, over the notes that have gone past.

// The music font (Bravura, a SMuFL font). Without it the staff still works, drawn with plain shapes.
bool loadStaffFont(const std::string& path);
void unloadStaffFont();

// How wide the clef, key and time signature are for a staff this tall: the hit line must sit to the right of them.
// Sized for the song's widest key signature, so it doesn't move mid-song.
float staffLeadWidth(float areaHeight, const Score& score);

// Room a downbeat note needs before it, sharp or flat included, for a staff this tall (see TimeAxis::barLineGap)
float staffBarLineGap(float areaHeight);

// `notes` are the track's notes in the score's order (for pitches and hit colors)
void drawStaff(Rectangle area, const std::vector<PlayNote>& notes, const Score& score, const TimeAxis& axis);
