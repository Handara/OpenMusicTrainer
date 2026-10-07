#pragma once

#include "imgui.h"

#include <string>
#include <vector>

// A list to choose from, in hardthz's style: readable rows, a brass string for a cursor that glides to the selected row
// and rings when plucked, and a note of a pentatonic scale for each move. The main menu and every screen that picks
// one thing from a list (songs, exercises, lessons) use it, so they all look, sound and behave alike.
//
// Arrows move, Enter or Space confirms, the mouse hovers and clicks, the wheel scrolls a list taller than its area.
// Headings sit between rows and are skipped. Shortcut keys (1, 2...) are the screen's to handle: it calls
// menuListSelect with `confirm` to answer them the same way.

struct MenuRow {
    std::string label;
    std::string key;       // a shortcut shown in its own column before the label ("1", "Esc"), empty for none
    std::string detail;    // dim text after the label: an artist, progress
    std::string note;      // a line under the row, in the "bad" color: why a file can't be used
    bool heading = false;  // a category title between rows: can't be selected
    bool disabled = false; // shown dim; can be selected (to read its note) but not confirmed
};

// A plain row: something to do, with an optional shortcut ("Back", "Esc")
inline MenuRow actionRow(const std::string& label, const std::string& key = ""){
    MenuRow row;
    row.label = label;
    row.key = key;
    return row;
}

// What a list remembers between frames. Keep one per screen: it keeps the selection when the screen comes back.
struct MenuList {
    int selected = -1;          // a row index; -1 = the first row that can be selected
    float stringY = -1.0f;      // where the string is drawn: it glides to the selected row
    float scroll = 0.0f;        // how far the rows are scrolled up, in pixels
    std::vector<float> shift;   // each row's step to the right, easing in and out
    double ringStart = -100.0;  // when the string was last plucked
    float ringStrength = 1.0f;
};

struct MenuListArea {
    ImVec2 topLeft;       // where the shortcut column starts; the string runs from the screen's left edge to here
    float width;
    float height;         // rows beyond it scroll
    float scale;          // 1 at a 720-pixel-tall window
};

// Draws the rows in the current window and handles input. Returns the row confirmed this frame, or -1.
int menuList(MenuList& list, const std::vector<MenuRow>& rows, const MenuListArea& area);

// Selects a row (and plucks the string, with its note); `confirm` answers as a confirmation does. For shortcut keys,
// and for Esc moving to Back.
void menuListSelect(MenuList& list, const std::vector<MenuRow>& rows, int row, bool confirm = false);

// The screens' common frame: a title at the top left (the list's left edge), and a line of hints at the bottom
void menuScreenTitle(const char* title, float scale);
void menuScreenHint(const char* hint, float scale);
float menuScale(); // the window's height against 720 pixels, the size the menus are designed at

// A labelled row of choices beside a screen's title ("MODE  PLAY  PRACTICE  EDIT"), the chosen one underlined in the
// accent; true when a click chose another
bool menuSwitchRow(const char* label, const char* const* names, int count, int& chosen, float x, float y, float scale);

// A small rounded button in the menus' style: its text, a chevron (`arrow` -1 before it pointing left, +1 after it
// pointing right, 0 none) and its shortcut key shown dim beside it (null for none). `anchor` is its top left, or its
// top right when `alignRight`. Drawn over everything; true when clicked.
bool menuPill(const char* text, const char* key, ImVec2 anchor, bool alignRight, int arrow, float scale);

// The way back, at the top left of every screen that has one, above its title: for the mouse. Esc and the mouse's
// own back button do the same (see main.cpp). Drawn over everything; true when clicked.
bool menuBackButton(float scale);
