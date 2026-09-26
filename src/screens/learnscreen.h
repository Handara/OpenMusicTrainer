#pragma once

#include <string>

// Learn mode: the exercises (built-in and the player's own .exercise files), and the one being practiced.

struct LearnFolders {
    std::string builtInExercises; // shipped with the game, read-only
    std::string userExercises;    // the player's own and downloaded ones
    std::string progress;         // one progress file per exercise
};

void openLearnScreen(const LearnFolders& folders); // scans both exercise folders
bool learnScreen();  // draws the menu or the running exercise; true when the player leaves learn mode
bool learnBack();    // Esc: ends the running exercise, or (from the menu) returns true to leave learn mode
void closeLearnScreen();
