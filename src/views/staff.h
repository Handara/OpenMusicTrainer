#pragma once

#include "core/score.h"
#include "raylib.h"
#include "views/playnote.h"

#include <string>
#include <vector>

// Sheet music, a bar at a time: the engraved score (core/score) on a staff, the bar being played filling most of it
// and the next one waiting beside it; the page turns at each bar line. The note, chord or rest being played is lit,
// played notes stay green or red. Written the guitar way (an octave above how it sounds).

// The music font (Bravura, a SMuFL font). Without it the staff still works, drawn with plain shapes.
bool loadStaffFont(const std::string& path);
void unloadStaffFont();

// How wide the clef, key and time signature are for a staff this tall: the hit line must sit to the right of them.
// Sized for the song's widest key signature, so it doesn't move mid-song.
float staffLeadWidth(float areaHeight, const Score& score);

// Room a downbeat note needs before it, sharp or flat included, for a staff this tall (see TimeAxis::barLineGap)
float staffBarLineGap(float areaHeight);

// `notes` are the track's notes in the score's order (for pitches and hit colors). Only the axis's song time is used.
void drawStaff(Rectangle area, const std::vector<PlayNote>& notes, const Score& score, const TimeAxis& axis);
// Everything drawStaff draws kept inside `clip` (a scrolled page's room), until it's given nullptr
void setStaffClip(const Rectangle* clip);
// Every bar side by side, the same width each, none faded or turned to (an example on a page, not music playing);
// until it's set back
void setStaffAllBars(bool all);
// The staff position (0 the bottom line, 1 the space above...) at a height in a staff drawn in `area`
int staffPositionAt(Rectangle area, float y);
// Where a note is in the staff as it was last drawn (over it), for showing its judgement there. False when the staff
// isn't being drawn, or the note isn't on the page.
bool staffNoteAt(int noteIndex, float& x, float& y);
