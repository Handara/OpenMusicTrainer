#pragma once

#include <string>

// Learn mode: a menu of exercises, and the exercise being practiced.

void openLearnScreen(const std::string& progressDir); // where each exercise keeps its progress file
bool learnScreen();  // draws the menu or the running exercise; true when the player leaves learn mode
bool learnBack();    // Esc: ends the running exercise, or (from the menu) returns true to leave learn mode
void closeLearnScreen();
