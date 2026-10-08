#pragma once

#include "core/settings.h"

// The first time lahn starts (no settings yet): a welcome, for someone who may never have played. Which instrument
// they're learning; plugging it in and playing a note (a meter moves, the note heard is named, so they know it works);
// how much a day; then straight into the first course, or the main menu.

enum class WelcomeChoice { None, MainMenu, FirstSteps, InputSettings };

void openWelcomeScreen();
void closeWelcomeScreen();
// The choices land in `settings` (the instrument heard, Learn on the piano, the daily goal) as they're made
WelcomeChoice welcomeScreen(Settings& settings);
bool welcomeBack(); // Esc: a step back; true on the first step (nothing to go back to)
