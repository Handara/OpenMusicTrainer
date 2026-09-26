#pragma once

#include <string>
#include <vector>

struct SongEntry {
    std::string folder;     // the song's folder: chart, audio, and later cover art
    std::string chartPath;
    std::string title;  // falls back to the folder name if the chart can't be read
    std::string artist;
    std::string error;  // why the chart failed to load, empty if it's playable; shown so chart authors see it
    bool builtIn;       // ships with the game (read-only) rather than living in the user's data folder
};

// Finds every <songsDir>/<folder>/song.chart, sorted by title
std::vector<SongEntry> scanSongs(const std::string& songsDir, bool builtIn);
