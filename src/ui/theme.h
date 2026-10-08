#pragma once

#include "imgui.h"
#include "raylib.h"

#include <string>

// lahn's look: a calm palette with one bright accent, the fonts, and the wordmark. Everything that draws menus takes
// its colors from here by role, never as fixed values, so light and dark both work.

enum class ThemeMode { Light, Dark };

// Colors by what they're for
enum class UiColor {
    Background, // the screen
    Card,       // panels that sit on it
    Ink,        // text and marks
    Dim,        // secondary text, what isn't selected
    StaffLine,  // the faint lines the screen sits on, and quiet borders
    Accent,     // the one bright color (cyan), for what matters right now
    Good,       // right, passed, in tune
    Bad,        // wrong, missed
};

// Loads the fonts and the wordmark and styles ImGui: after InitWindow and ImGui's setup
void initTheme(const std::string& resourcesDir, ThemeMode mode);
void closeTheme();
void setTheme(ThemeMode mode); // restyles ImGui at once
ThemeMode currentTheme();

Color themeColor(UiColor role);
// Accessibility (the settings'): good and bad in blue and orange (told apart with any colour vision), less motion,
// everything a size bigger or smaller
void setAccessibility(bool colorBlind, bool reduceMotion, float uiScale);
bool reducedMotion();
float uiScaleSetting();
ImU32 uiColor(UiColor role, float alpha = 1.0f);
ImU32 mixColor(ImU32 a, ImU32 b, float t); // the color between two (t 0: the first, 1: the second), opaque
ImVec4 uiColorVec(UiColor role, float alpha = 1.0f);

// The fonts, for ImGui::PushFont(font, size); each is null if its file is missing (ImGui's own is used then)
struct UiFonts {
    ImFont* text = nullptr;   // Figtree Medium: everything you read
    ImFont* bold = nullptr;   // Figtree Bold: menu items, headings
    ImFont* heavy = nullptr;  // Figtree ExtraBold: the wordmark, big numbers
    ImFont* mono = nullptr;   // Chivo Mono: labels, readouts, shortcut keys
};
const UiFonts& uiFonts();

// Straight lines as thin rectangles: crisp at any thickness, where ImGui's anti-aliased lines can come out uneven
void horizontalLine(ImDrawList* draw, float x0, float x1, float y, float thickness, ImU32 color);
void verticalLine(ImDrawList* draw, float x, float y0, float y1, float thickness, ImU32 color);

// "lahn | لحن": the name in both scripts, split by a string. `height` is the Latin letters' size in pixels.
// Returns the width drawn.
float drawWordmark(ImDrawList* draw, ImVec2 topLeft, float height);
