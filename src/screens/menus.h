#pragma once

#include "core/songlibrary.h"
#include "screens/gameplay.h"

#include <string>
#include <vector>

// Menu screens. Each one draws itself for the current frame and returns what the player chose.
// They never switch to another screen themselves: main.cpp decides what happens next,
// so the whole flow of the app reads in one place.

enum class MainMenuChoice { None, Play, Learn, Editor, LessonEditor, Tuner, Instrument, Settings, Quit }; // see screens/mainmenu

struct SongSelectChoice {
    bool back = false;
    bool openDataFolder = false;
    bool newSong = false; // editing only: make a new song from an audio file
    int songIndex = -1; // index into the songs list, -1 if nothing was picked this frame
    int part = 0;       // which of the song's parts to play
    bool practice = false;   // practising part of it rather than playing it through (Tab switches it on the song list)
    bool partsOpened = false; // the instruments to play the song with were just listed: see whether they're connected
    bool importSong = false;  // bring in a song: from a Guitar Pro tab, a recording of its bass, or the song itself
    int deleteSong = -1;      // the song to delete (the player's own, confirmed already), -1 for none
};
// Whether an instrument can be played now: its input device is there. `problem` says why not, for the player.
struct InstrumentStatus {
    bool ready = true;
    std::string problem;
};
// forEditing marks built-in songs, since editing one creates a copy.
// Playing, confirming a song lists its parts by instrument (its guitar, its bass, its keys): each is played on its
// own instrument, and one that isn't connected (`guitar`, `bass`) can't be chosen, with why.
// `notice` is good news to show (a song just added), `error` bad news.
SongSelectChoice songSelectScreen(const char* title, const std::vector<SongEntry>& songs, const std::string& error,
                                  const std::string& notice, bool forEditing, const InstrumentStatus& guitar = {},
                                  const InstrumentStatus& bass = {});
// The song list lands on this song next time it's shown (one just added)
void selectSongInList(int songIndex);
// Esc on the song list: closes the list of parts, or the question before a song is deleted, if it's open (true: it was,
// the screen stays)
bool songSelectBack();

enum class PauseChoice { None, Resume, Retry, PracticeSettings, SwitchMode, Tune, Quit };
// Over the paused play screen: the song, and what to do
// `practising`: the pause is in a practice (its rows say so). `instrument`: "bass" or "guitar", to offer tuning it;
// null for none (a keys part)
// `canSwitch`: practising and playing through can be switched between (not in a test play from the editor)
PauseChoice pauseScreen(const std::string& song, bool practising, const char* instrument, bool canSwitch = true);

// Over the play screen, paused because the instrument sounds out of tune (`cents` off, + sharp): tune it and start
// the song over, or play on
enum class OutOfTuneChoice { None, Retune, PlayOn, Quit };
OutOfTuneChoice outOfTuneScreen(const std::string& song, const char* instrument, float cents);

enum class ResultsChoice { None, Retry, BackToSongs };
ResultsChoice resultsScreen(const GameResult& result);

void tunerScreen();
