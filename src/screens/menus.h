#pragma once

#include "core/songlibrary.h"
#include "screens/gameplay.h"

#include <string>
#include <vector>

// Menu screens. Each one draws itself for the current frame and returns what the player chose.
// They never switch to another screen themselves: main.cpp decides what happens next,
// so the whole flow of the app reads in one place.

enum class MainMenuChoice { None, Play, Editor, Tuner, Settings, Quit };
MainMenuChoice mainMenuScreen(const std::string& error);

struct SongSelectChoice {
    bool back = false;
    bool openDataFolder = false;
    int songIndex = -1; // index into the songs list, -1 if nothing was picked this frame
};
// forEditing marks built-in songs, since editing one creates a copy
SongSelectChoice songSelectScreen(const char* title, const std::vector<SongEntry>& songs, const std::string& error,
                                  bool forEditing);

enum class ResultsChoice { None, Retry, BackToSongs };
ResultsChoice resultsScreen(const GameResult& result);

bool tunerScreen(); // true when the player pressed Back
