#pragma once

#include <string>

// Song packages: a whole song in one file to share, a .lahn file. It's a plain zip (any zip tool opens it) holding
// the song's files side by side: song.chart, the audio the chart names, its video if it names one, and cover.png or
// cover.jpg if the song has one. Nothing else goes in.

const char* const SONG_PACKAGE_EXTENSION = ".lahn";

// Packs the song in `songFolder` into `packagePath` (replacing any file there only once the new one is complete).
// `withVideo`: its video too, when it has one; without, the package is much smaller and the song plays the same.
bool exportSongPackage(const std::string& songFolder, const std::string& packagePath, std::string& error, bool withVideo = true);

// Unpacks a package into a new folder of `songsDir`, named after the song's title (" (2)" and on if it's taken).
// Checked first: only plain file names, a song.chart that loads, the audio it names. On failure nothing is left
// behind. `installedFolder` is the new song's folder.
bool installSongPackage(const std::string& packagePath, const std::string& songsDir, std::string& installedFolder,
                        std::string& error);
