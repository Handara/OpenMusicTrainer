#pragma once

#include "screens/menus.h"

#include <string>

// The main menu, lahn's front door: the wordmark, a readable list, and a string that points at the selected item and
// rings when it moves. Each item is a note of a pentatonic scale, so moving through the menu always sounds musical.
// Five faint staff lines are the grid the screen sits on. On the right, "today": the routine to keep up and the next
// goal, read from the player's progress each time the menu appears.

// What the menu shows besides its items
struct MainMenuInfo {
    std::string inputDevice;      // for the footer: what the game listens to
    std::string builtInExercises; // for the "today" panel: the exercises, and the player's progress with them
    std::string userExercises;
    std::string progressDir;
};

MainMenuChoice mainMenuScreen(const MainMenuInfo& info, const std::string& error);
