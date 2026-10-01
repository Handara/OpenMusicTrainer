#include "doctest/doctest.h"

#include "core/chart.h"
#include "core/files.h"
#include "core/songlibrary.h"

#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

// An empty songs folder in the temp directory, and a stand-in audio file (createSong only copies it)
static fs::path songsFolder(){
    fs::path root = fs::temp_directory_path() / "lahn_tests" / "new_songs";
    fs::remove_all(root);
    fs::create_directories(root / "songs");
    std::ofstream(root / "My Take.OGG", std::ios::binary) << "not really audio";
    return root;
}

TEST_CASE("folder names keep only what works on every system"){
    CHECK(safeFolderName("Scales in G") == "Scales in G");
    CHECK(safeFolderName("  AC/DC: Back in Black?  ") == "ACDC Back in Black");
    CHECK(safeFolderName("my-song_2") == "my-song_2");
    CHECK(safeFolderName("???") == "");
}

TEST_CASE("a new song from an audio file: its folder, its audio and a starter chart"){
    fs::path root = songsFolder();
    NewSong song;
    song.audioPath = (root / "My Take.OGG").string();
    song.title = "Evening: take 2";
    song.artist = "Me";
    song.bpm = 120.0;
    song.lengthSeconds = 9.0; // bars last 2 s at 120 bpm: 4.5 bars, so the chart covers 5
    std::string chartPath, error;
    REQUIRE_MESSAGE(createSong((root / "songs").string(), song, chartPath, error), error);

    fs::path folder = root / "songs" / "Evening take 2";
    CHECK(chartPath == (folder / "song.chart").string());
    CHECK(fs::exists(folder / "audio.ogg")); // a fixed name, the extension in lowercase

    Chart chart;
    REQUIRE_MESSAGE(loadChart(chartPath, chart, error), error);
    CHECK(chart.title == "Evening: take 2");
    CHECK(chart.artist == "Me");
    CHECK(chart.audioFile == "audio.ogg");
    CHECK(chart.tempoMap[0].bpm == doctest::Approx(120.0));
    CHECK(chart.endTick == 5 * 4 * chart.resolution);
    REQUIRE(chart.frettedTracks.size() == 1);
    CHECK(chart.frettedTracks[0].tuning.size() == 6);
    CHECK(chart.frettedTracks[0].notes.empty());

    SUBCASE("the same title again is refused, and the first song stays"){
        CHECK_FALSE(createSong((root / "songs").string(), song, chartPath, error));
        CHECK(error.find("already") != std::string::npos);
        CHECK(fs::exists(folder / "song.chart"));
    }
}

TEST_CASE("a new song is refused without what it needs, leaving nothing behind"){
    fs::path root = songsFolder();
    NewSong song;
    song.audioPath = (root / "My Take.OGG").string();
    song.title = "Fine";
    song.lengthSeconds = 5.0;
    std::string chartPath, error;

    SUBCASE("no usable title"){ song.title = "???"; }
    SUBCASE("a tempo out of range"){ song.bpm = 0.0; }
    SUBCASE("no audio file there"){ song.audioPath = (root / "missing.wav").string(); }

    CHECK_FALSE(createSong((root / "songs").string(), song, chartPath, error));
    CHECK_FALSE(error.empty());
    CHECK(fs::is_empty(root / "songs"));
}

TEST_CASE("a deleted song goes to the trash folder, where it can be found again"){
    namespace fs = std::filesystem;
    fs::path root = fs::temp_directory_path() / "lahn_tests" / "trash";
    fs::remove_all(root);
    fs::path song = root / "songs" / "My riff", trash = root / "trash";
    fs::create_directories(song);
    { std::ofstream(song / "song.chart") << "x"; }
    std::string error;
    REQUIRE_MESSAGE(trashSong(song.string(), trash.string(), error), error);
    CHECK_FALSE(fs::exists(song));
    CHECK(fs::is_regular_file(trash / "My riff" / "song.chart"));

    // Another of the same name deleted later sits beside the first
    fs::create_directories(song);
    REQUIRE(trashSong(song.string(), trash.string(), error));
    CHECK(fs::is_directory(trash / "My riff (2)"));
    CHECK_FALSE(trashSong(song.string(), trash.string(), error)); // it's gone already
}
