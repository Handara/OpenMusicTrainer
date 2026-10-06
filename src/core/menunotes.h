#pragma once

#include <string>
#include <vector>

// The instrument as a controller in the menus, so a player never has to put it down for the keyboard. The open
// strings are arrows, the same everywhere:
//   guitar: low E back, A up, D down, G choose, B left, high E right
//   bass:   E back, A up, D down, G choose; the G string's 4th fret left, its 5th right
// and on a short menu each item has a note of its own, played to go straight there: the first position's natural
// notes that aren't open strings (F and G on the low E string, B and C on the A string...), the notes a beginner
// learns first.

enum class MenuNoteAction { None, Back, Up, Down, Choose, Left, Right, Item };

struct MenuNoteMap {
    int back, up, down, choose, left, right; // MIDI
    std::vector<int> items;                  // the first item's note, the second's...
    std::vector<int> tuning;                 // the instrument's, lowest string first
};
const MenuNoteMap& menuNoteMap(bool bass);

// What a note heard does; for an item, which one (from 0) in `item`
MenuNoteAction menuNoteAction(const MenuNoteMap& map, int pitch, int& item);

// Where a note is played, short, for a chip beside an item: "low E 1", "A 3", "G open"
std::string menuNotePlace(const MenuNoteMap& map, int pitch);
