#pragma once

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
