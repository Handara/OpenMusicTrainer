#include "core/songlibrary.h"

#include "core/chart.h"

#include <algorithm>
#include <filesystem>

namespace fs = std::filesystem;

std::vector<SongEntry> scanSongs(const std::string& songsDir, bool builtIn){
    std::vector<SongEntry> songs;
    std::error_code ec; // a missing songs folder just means an empty list, not a crash
    for (const fs::directory_entry& entry : fs::directory_iterator(songsDir, ec)){
        fs::path chartPath = entry.path() / "song.chart";
        if (!entry.is_directory() || !fs::exists(chartPath)) continue;

        SongEntry song;
        song.folder = entry.path().string();
        song.chartPath = chartPath.string();
        song.builtIn = builtIn;
        song.title = entry.path().filename().string();

        // Loads the whole chart just for its title: fine for small libraries, revisit if scanning gets slow
        Chart chart;
        if (loadChart(song.chartPath, chart, song.error)){
            if (!chart.title.empty()) song.title = chart.title;
            song.artist = chart.artist;
        }
        songs.push_back(song);
    }
    std::sort(songs.begin(), songs.end(), [](const SongEntry& a, const SongEntry& b){ return a.title < b.title; });
    return songs;
}
