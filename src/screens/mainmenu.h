#pragma once

#include "screens/menus.h"

#include <string>

// The main menu, lahn's front door: the wordmark, a readable list, and a string that points at the selected item and
// rings when it moves. Each item is a note of a pentatonic scale, so moving through the menu always sounds musical.
// Five faint staff lines are the grid the screen sits on.

// What the menu shows besides its items
struct MainMenuInfo {
    std::string inputDevice; // for the footer: what the game listens to
};

MainMenuChoice mainMenuScreen(const MainMenuInfo& info, const std::string& error);
