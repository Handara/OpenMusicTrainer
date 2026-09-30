#pragma once

#include "core/settings.h"
#include "raylib.h"

#include <string>

// The text font of the note views: fret numbers in the tab and on the highway, the tab's T A B, the highway's string
// names. Without it, raylib's built-in pixel font is used.
bool loadViewFont(const std::string& path);
void unloadViewFont();

// Draws text with its middle (the middle of a digit or a capital) at `y`. `anchor` places it along x: 0 starts it at
// `x`, 0.5 centers it there.
void drawViewText(const char* text, float x, float y, float size, Color color, float anchor = 0.5f);
float viewTextWidth(const char* text, float size);
// What a note says (views/highway, views/neckview), centered at x, y: its fret, its name ("F#"), or the fret with the
// name small under it. `size` is the fret number's size alone.
void drawNoteLabel(int fret, int pitch, NoteLabel label, float x, float y, float size, Color color);
