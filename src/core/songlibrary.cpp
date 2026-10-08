#include "core/songlibrary.h"

#include "core/backing.h"
#include "core/chart.h"
#include "core/difficulty.h"
#include "core/files.h"

#include <algorithm>
#include <cctype>
#include <cmath>
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
            for (int part = 0; part < (int)chart.frettedTracks.size(); part++){
                const FrettedTrack& track = chart.frettedTracks[part];
                SongPart info;
                info.name = track.name;
                info.type = track.type;
                info.stringCount = (int)track.tuning.size();
                info.tuning = track.tuning;
                info.fingerprint = partFingerprint(chart, part);
                info.stars = partStars(chart, part);
                song.parts.push_back(info);
            }
            for (int keys = 0; keys < (int)chart.keysTracks.size(); keys++){
                SongPart info;
                info.name = chart.keysTracks[keys].name;
                info.type = InstrumentType::Keys;
                info.stringCount = 0;
                info.fingerprint = partFingerprint(chart, (int)chart.frettedTracks.size() + keys);
                info.stars = partStars(chart, (int)chart.frettedTracks.size() + keys);
                song.parts.push_back(info);
            }
        } else if (song.error.rfind(song.chartPath, 0) == 0){
            // Menus show the error: "folder/song.chart" is enough there, the full path would take several lines
            song.error = (entry.path().filename() / "song.chart").generic_string() + song.error.substr(song.chartPath.size());
        }
        songs.push_back(song);
    }
    std::sort(songs.begin(), songs.end(), [](const SongEntry& a, const SongEntry& b){ return a.title < b.title; });
    return songs;
}

std::string songId(const SongEntry& song){
    return (song.builtIn ? "builtin-" : "user-") + fs::path(song.folder).filename().string();
}

void loadBestRuns(std::vector<SongEntry>& songs, const std::string& recordsDir){
    for (SongEntry& song : songs){
        for (int part = 0; part < (int)song.parts.size(); part++){
            SongPart& info = song.parts[part];
            std::string path = recordsPath(recordsDir, songId(song), part, info.fingerprint);
            std::vector<RunRecord> records = loadRuns(path);
            info.played = !records.empty();
            if (info.played) info.best = records.front();
            info.history = loadHistory(historyPath(path), records);
            std::string rhythmPath = recordsPath(recordsDir, songId(song), part, info.fingerprint + "-rhythm");
            std::vector<RunRecord> rhythm = loadRuns(rhythmPath);
            info.playedRhythm = !rhythm.empty();
            if (info.playedRhythm) info.bestRhythm = rhythm.front();
            info.historyRhythm = loadHistory(historyPath(rhythmPath), rhythm);
        }
    }
}

bool trashSong(const std::string& songFolder, const std::string& trashDir, std::string& error){
    std::error_code ec;
    if (!fs::is_directory(songFolder, ec)){
        error = "The song's folder isn't there any more";
        return false;
    }
    fs::create_directories(trashDir, ec);
    std::string name = fs::path(songFolder).filename().string();
    fs::path destination = fs::path(trashDir) / name;
    for (int n = 2; fs::exists(destination, ec); n++) destination = fs::path(trashDir) / (name + " (" + std::to_string(n) + ")");
    fs::rename(songFolder, destination, ec);
    if (ec){
        error = "Could not delete the song: " + ec.message();
        return false;
    }
    return true;
}

bool createImportedSong(const std::string& songsDir, Chart chart, const std::string& audioPath, int sampleRate,
                        std::string& chartPath, std::string& error){
    std::string base = safeFolderName(chart.title);
    if (base.empty()) base = "Imported song";
    std::error_code ec;
    fs::path folder = fs::path(songsDir) / base;
    for (int n = 2; fs::exists(folder, ec); n++) folder = fs::path(songsDir) / (base + " " + std::to_string(n));
    fs::create_directories(folder, ec);
    if (ec){
        error = "Could not make the song's folder: " + ec.message();
        return false;
    }
    if (chart.title.empty()) chart.title = base;
    if (!audioPath.empty()){
        std::string extension = fs::path(audioPath).extension().string();
        for (char& c : extension) c = (char)std::tolower((unsigned char)c);
        chart.audioFile = "audio" + extension;
        fs::copy_file(audioPath, folder / chart.audioFile, ec);
        if (ec) error = "Could not copy the audio: " + ec.message();
    } else {
        chart.audioFile = "backing.wav";
        chart.offset = 0.0;
        writeWav((folder / chart.audioFile).string(), renderBacking(chart, sampleRate), sampleRate, error);
    }
    chartPath = (folder / "song.chart").string();
    if (!error.empty() || !saveChart(chartPath, chart, error)){
        fs::remove_all(folder, ec);
        return false;
    }
    return true;
}

bool createSong(const std::string& songsDir, const NewSong& song, std::string& chartPath, std::string& error){
    std::string folderName = safeFolderName(song.title);
    if (folderName.empty()){
        error = "Give the song a title (letters, digits, spaces, - and _)";
        return false;
    }
    if (!(song.bpm >= 1.0 && song.bpm <= 1000.0)){
        error = "The tempo must be between 1 and 1000 BPM";
        return false;
    }
    std::error_code ec;
    if (!fs::is_regular_file(song.audioPath, ec)){
        error = "No audio file at '" + song.audioPath + "'";
        return false;
    }
    fs::path folder = fs::path(songsDir) / folderName;
    if (fs::exists(folder, ec)){
        error = "There's already a song folder called '" + folderName + "'";
        return false;
    }

    // audio.ogg, audio.wav...: a fixed name, so the chart never depends on what the file was called
    std::string extension = fs::path(song.audioPath).extension().string();
    for (char& c : extension) c = (char)std::tolower((unsigned char)c);
    std::string audioFile = "audio" + extension;
    fs::create_directories(folder, ec);
    if (!ec) fs::copy_file(song.audioPath, folder / audioFile, ec);
    if (ec){
        error = "Could not copy the audio: " + ec.message();
        fs::remove_all(folder, ec);
        return false;
    }

    Chart chart{};
    chart.version = 2;
    chart.title = song.title;
    chart.artist = song.artist;
    chart.audioFile = audioFile;
    chart.resolution = 480;
    chart.offset = 0.0;
    chart.tempoMap = {{0, song.bpm}};
    chart.timeSignatures = {{0, 4, 4}};
    chart.keys = {{0, KeySignature{}}};
    double barSeconds = 4 * 60.0 / song.bpm;
    int bars = std::max(1, (int)std::ceil(song.lengthSeconds / barSeconds - 1e-9));
    chart.endTick = bars * 4 * chart.resolution;
    FrettedTrack guitar;
    guitar.type = InstrumentType::Guitar;
    guitar.name = "Guitar";
    guitar.tuning = { 40, 45, 50, 55, 59, 64 };
    chart.frettedTracks = {guitar};

    chartPath = (folder / "song.chart").string();
    if (!saveChart(chartPath, chart, error)){
        fs::remove_all(folder, ec);
        return false;
    }
    return true;
}
