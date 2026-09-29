#pragma once

#include "core/songlibrary.h"
#include "screens/gameplay.h"

#include <string>
#include <vector>

// Menu screens. Each one draws itself for the current frame and returns what the player chose.
// They never switch to another screen themselves: main.cpp decides what happens next,
// so the whole flow of the app reads in one place.

enum class MainMenuChoice { None, Play, Learn, Editor, LessonEditor, Tuner, Settings, Quit }; // see screens/mainmenu

struct SongSelectChoice {
    bool back = false;
    bool openDataFolder = false;
    bool newSong = false; // editing only: make a new song from an audio file
    int songIndex = -1; // index into the songs list, -1 if nothing was picked this frame
    int part = 0;       // which of the song's parts to play
    bool rhythmMode = false; // taiko-style, only the rhythm (Tab switches it on the song list)
};
// forEditing marks built-in songs, since editing one creates a copy
// Playing a song with several parts (guitar, bass), confirming it lists its parts to choose from first.
// `notice` is good news to show (a song just added), `error` bad news.
SongSelectChoice songSelectScreen(const char* title, const std::vector<SongEntry>& songs, const std::string& error,
                                  const std::string& notice, bool forEditing);
// Esc on the song list: closes the list of parts if it's open (true: it was, the screen stays)
bool songSelectBack();

enum class PauseChoice { None, Resume, Retry, Quit };
// Over the paused play screen: the song, and what to do
PauseChoice pauseScreen(const std::string& song);

enum class ResultsChoice { None, Retry, BackToSongs };
ResultsChoice resultsScreen(const GameResult& result);

bool tunerScreen(); // true when the player pressed Back
