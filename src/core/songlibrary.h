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

// A new song from someone's own audio file
struct NewSong {
    std::string audioPath;  // the file to copy in
    std::string title;
    std::string artist;
    double bpm = 120.0;
    double lengthSeconds = 0.0; // the audio's length (the caller measures it): the chart covers all of it
};

// Makes <songsDir>/<title as a folder name>/ with the audio copied in as audio.<its extension>, and a starter chart:
// one guitar track in standard tuning, 4/4 in C major at the given tempo, long enough for the whole audio, no notes.
// Returns the chart's path in `chartPath`; nothing is left behind if it fails.
bool createSong(const std::string& songsDir, const NewSong& song, std::string& chartPath, std::string& error);
