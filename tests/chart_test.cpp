#include "doctest/doctest.h"

#include "core/chart.h"

#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

static std::string tempPath(const std::string& name){
    fs::path dir = fs::temp_directory_path() / "omt_tests";
    fs::create_directories(dir);
    return (dir / name).string();
}

static std::string writeTemp(const std::string& name, const std::string& content){
    std::string path = tempPath(name);
    std::ofstream(path, std::ios::binary) << content;
    return path;
}

static const std::string HEADER = "version 1\nresolution 480\nend 9600\ntempo 0 120\n";
static const std::string TRACK = "track guitar Lead\ntuning 40 45 50 55 59 64\n";

TEST_CASE("the test chart shipped with the game loads"){
    Chart chart;
    std::string error;
    REQUIRE_MESSAGE(loadChart(OMT_RESOURCES_DIR "songs/test-pattern/song.chart", chart, error), error);
    CHECK(chart.title == "Test Pattern");
    CHECK(chart.audioFile == "audio.wav");
    REQUIRE(chart.frettedTracks.size() == 1);
    CHECK(chart.frettedTracks[0].tuning.size() == 6);
    CHECK(chart.frettedTracks[0].notes.size() == 12);
}

TEST_CASE("tick to seconds follows the tempo map"){
    Chart chart{};
    chart.resolution = 480;
    chart.tempoMap = {{0, 120.0}};
    CHECK(tickToSeconds(chart, 960) == doctest::Approx(1.0));   // 2 beats at 120 bpm
    CHECK(tickToSeconds(chart, 7200) == doctest::Approx(7.5));

    chart.tempoMap = {{0, 120.0}, {960, 60.0}};
    CHECK(tickToSeconds(chart, 960) == doctest::Approx(1.0));   // exactly on the tempo change
    CHECK(tickToSeconds(chart, 1440) == doctest::Approx(2.0));  // 1 s at 120 bpm + 1 beat at 60 bpm

    chart.offset = 0.25;
    CHECK(tickToSeconds(chart, 0) == doctest::Approx(0.25));
}

TEST_CASE("accepted variations"){
    Chart chart;
    std::string error;
    // Windows line endings, a bass, a name with spaces, an optional duration, notes out of order
    std::string path = writeTemp("good.chart",
        HEADER + "track bass Bass Guitar\r\ntuning 28 33 38 43\r\nn 960 3 5 240\r\nn 480 0 0\r\n");
    REQUIRE_MESSAGE(loadChart(path, chart, error), error);
    const FrettedTrack& track = chart.frettedTracks[0];
    CHECK(track.type == InstrumentType::Bass);
    CHECK(track.name == "Bass Guitar");
    CHECK(track.notes[0].tick == 480); // sorted on load
    CHECK(track.notes[1].duration == 240);
}

TEST_CASE("broken charts are rejected with a clear message"){
    struct Case { const char* name; std::string content; const char* expectedMessage; };
    const Case cases[] = {
        {"empty file",          "",                                        "missing 'version'"},
        {"bad number",          HEADER + TRACK + "n abc 0 0\n",            "expected: n <tick> <string> <fret>"},
        {"trailing garbage",    HEADER + TRACK + "n 960 0 0 12 extra\n",   "unexpected text after 'n'"},
        {"string out of range", HEADER + TRACK + "n 960 6 0\n",            "string must be 0-5"},
        {"fret out of range",   HEADER + TRACK + "n 960 0 25\n",           "fret must be 0-24"},
        {"note before tuning",  HEADER + "track guitar Lead\nn 960 0 0\n", "note before its track's tuning"},
        {"unknown track type",  HEADER + "track banjo X\n",                "unknown track type 'banjo'"},
        {"unknown keyword",     HEADER + TRACK + "bmp 120\n",              "unknown keyword 'bmp'"},
        {"newer version",       "version 2\n" + HEADER.substr(10) + TRACK, "newer than this build supports"},
        {"no tempo at tick 0",  "version 1\nresolution 480\nend 9600\ntempo 480 120\n" + TRACK, "needs a tempo at tick 0"},
        {"duplicate note",      HEADER + TRACK + "n 960 0 0\nn 960 0 3\n", "two notes on string 0 at tick 960"},
        {"note after end",      HEADER + TRACK + "n 99999 0 0\n",          "is after 'end'"},
        {"no tracks",           HEADER,                                    "chart has no tracks"},
    };
    for (const Case& c : cases){
        SUBCASE(c.name){
            Chart chart;
            std::string error;
            CHECK_FALSE(loadChart(writeTemp("bad.chart", c.content), chart, error));
            CHECK_MESSAGE(error.find(c.expectedMessage) != std::string::npos, error);
        }
    }

    Chart chart;
    std::string error;
    CHECK_FALSE(loadChart(tempPath("does_not_exist.chart"), chart, error));
    CHECK(error.find("could not open file") != std::string::npos);
}

TEST_CASE("save then load gives back the same chart"){
    Chart original{};
    original.version = 1;
    original.title = "Round Trip";
    original.artist = "Some Artist";
    original.audioFile = "song.ogg";
    original.resolution = 480;
    original.offset = 0.1;                   // not exactly representable in binary: must survive anyway
    original.endTick = 9600;
    original.tempoMap = {{0, 128.33333333333334}, {1920, 90.0}};
    FrettedTrack track;
    track.type = InstrumentType::Guitar;
    track.name = "Lead Guitar";
    track.tuning = {38, 45, 50, 55, 59, 64}; // drop D
    track.notes = {{0, 0, 0, 0}, {480, 2, 12, 240}, {480, 5, 24, 0}};
    original.frettedTracks = {track};

    std::string path = tempPath("roundtrip.chart");
    std::string error;
    REQUIRE_MESSAGE(saveChart(path, original, error), error);
    CHECK_FALSE(fs::exists(path + ".tmp")); // the temporary file was swapped in, not left behind

    Chart loaded;
    REQUIRE_MESSAGE(loadChart(path, loaded, error), error);
    CHECK(loaded.title == original.title);
    CHECK(loaded.artist == original.artist);
    CHECK(loaded.audioFile == original.audioFile);
    CHECK(loaded.resolution == original.resolution);
    CHECK(loaded.offset == original.offset); // exact, not approximate
    CHECK(loaded.endTick == original.endTick);
    REQUIRE(loaded.tempoMap.size() == 2);
    CHECK(loaded.tempoMap[0].bpm == original.tempoMap[0].bpm);
    CHECK(loaded.tempoMap[1].tick == 1920);
    REQUIRE(loaded.frettedTracks.size() == 1);
    const FrettedTrack& t = loaded.frettedTracks[0];
    CHECK(t.name == "Lead Guitar");
    CHECK(t.tuning == track.tuning);
    REQUIRE(t.notes.size() == 3);
    for (size_t i = 0; i < t.notes.size(); i++){
        CHECK(t.notes[i].tick == track.notes[i].tick);
        CHECK(t.notes[i].stringIndex == track.notes[i].stringIndex);
        CHECK(t.notes[i].fret == track.notes[i].fret);
        CHECK(t.notes[i].duration == track.notes[i].duration);
    }

    // Saving again over the existing file replaces it
    original.title = "Changed";
    REQUIRE(saveChart(path, original, error));
    REQUIRE(loadChart(path, loaded, error));
    CHECK(loaded.title == "Changed");
}
