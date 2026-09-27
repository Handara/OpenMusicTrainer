#pragma once

#include "core/settings.h"

#include <string>

// Learn mode: lessons, and the exercises (built-in and the player's own .exercise files), and the one being practiced.

struct LearnSetup {
    std::string builtInExercises; // shipped with the game, read-only
    std::string userExercises;    // the player's own and downloaded ones
    std::string builtInLessons;   // lessons: one folder each, built-in and the player's own
    std::string userLessons;
    std::string progress;         // one progress file per exercise
    Settings settings;            // input device, offsets, note view... as they are when learn mode opens
};

void openLearnScreen(const LearnSetup& setup); // scans both exercise folders
bool learnScreen();  // draws the menu or the running exercise; true when the player leaves learn mode
bool learnBack();    // Esc: ends the running exercise, or (from the menu) returns true to leave learn mode
void closeLearnScreen();
