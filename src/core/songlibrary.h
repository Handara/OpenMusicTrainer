#pragma once

#include "core/chart.h"
#include "core/ranking.h"

#include <string>
#include <vector>

// One part of a song, to choose from when there are several: "Lead" on guitar, "Bass" on bass
struct SongPart {
    std::string name;
    InstrumentType type;
    int stringCount;           // 0 for keys
    std::string fingerprint;   // its records are kept under it (core/ranking)
    bool played = false;       // filled in by whoever reads the records: the part's best run, if there's one...
    RunRecord best;
    bool playedRhythm = false; // ...and its best in rhythm mode
    RunRecord bestRhythm;
};

struct SongEntry {
    std::string folder;     // the song's folder: chart, audio, and later cover art
    std::string chartPath;
    std::string title;  // falls back to the folder name if the chart can't be read
    std::string artist;
    std::string error;  // why the chart failed to load, empty if it's playable; shown so chart authors see it
    bool builtIn;       // ships with the game (read-only) rather than living in the user's data folder
    std::vector<SongPart> parts; // its fretted tracks, in the chart's order
};

// The song's id, for its records: "builtin-<folder>" or "user-<folder>"
std::string songId(const SongEntry& song);

// Reads each part's best run from the records folder into the songs' parts
void loadBestRuns(std::vector<SongEntry>& songs, const std::string& recordsDir);

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
