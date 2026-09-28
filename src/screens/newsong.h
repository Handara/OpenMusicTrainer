#pragma once

#include <string>

// Making a new song from the player's own audio file: pick the file (type its path or drop it on the window), give
// the song a title, an artist and a tempo. The song is made in the user's songs folder (core/songlibrary
// createSong) and opens in the editor, where playback helps set the tempo and offset by ear.

void openNewSongScreen(const std::string& userSongsDir);

enum class NewSongChoice { None, Back, Created };
NewSongChoice newSongScreen();
std::string newSongChartPath(); // after Created: the new song's chart
