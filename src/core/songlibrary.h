#pragma once

#include <string>
#include <vector>

struct SongEntry {
    std::string chartPath;
    std::string title;  // falls back to the folder name if the chart can't be read
    std::string artist;
    std::string error;  // why the chart failed to load, empty if it's playable; shown so chart authors see it
};

// Finds every <songsDir>/<folder>/song.chart, sorted by title
std::vector<SongEntry> scanSongs(const std::string& songsDir);
