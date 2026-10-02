#pragma once

#include <string>

// Making a new song from the player's own audio file, or from a video (its sound becomes the audio: app/videoconvert,
// with the video add-on): pick the file (type its path or drop it on the window), give the song a title, an artist
// and a tempo. The song is made in the user's songs folder (core/songlibrary
// createSong) and opens in the editor, where playback helps set the tempo and offset by ear.

// addonsDir: where add-ons are installed. audioPath: the audio or the video, if already chosen.
void openNewSongScreen(const std::string& userSongsDir, const std::string& addonsDir, const std::string& audioPath = "");

enum class NewSongChoice { None, Back, Created };
NewSongChoice newSongScreen();
std::string newSongChartPath(); // after Created: the new song's chart
std::string newSongVideoPath(); // and the video it was made from ("" for a song from audio): its pictures are still to bring in
