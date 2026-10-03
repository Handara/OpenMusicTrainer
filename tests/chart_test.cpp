#include "doctest/doctest.h"

#include "core/chart.h"

#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

static std::string tempPath(const std::string& name){
    fs::path dir = fs::temp_directory_path() / "lahn_tests";
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

TEST_CASE("the sample song shipped with the game loads"){
    Chart chart;
    std::string error;
    REQUIRE_MESSAGE(loadChart(LAHN_RESOURCES_DIR "songs/first-light/song.chart", chart, error), error);
    CHECK(chart.title == "First Light");
    CHECK(chart.audioFile == "audio.wav");
    CHECK(chart.keys[0].key.fifths == 1);   // E minor
    REQUIRE(chart.frettedTracks.size() == 2); // the melody on guitar, and the bass line
    CHECK(chart.frettedTracks[0].tuning.size() == 6);
    CHECK(chart.frettedTracks[0].notes.size() == 38);
    CHECK(chart.frettedTracks[1].type == InstrumentType::Bass);
    CHECK(chart.frettedTracks[1].tuning.size() == 4);
    CHECK(chart.frettedTracks[1].notes.size() == 20);
    REQUIRE(chart.keysTracks.size() == 1);            // and the piano's chords
    CHECK(chart.keysTracks[0].name == "Piano");
    CHECK(chart.keysTracks[0].notes.size() == 62);    // 2 chords a bar: 9 bars of three-note chords, the B7 bar of four
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

TEST_CASE("seconds to tick undoes tick to seconds"){
    Chart chart{};
    chart.resolution = 480;
    chart.offset = 0.25;
    chart.tempoMap = {{0, 120.0}, {960, 60.0}, {1920, 180.0}};
    for (int tick : {0, 1, 480, 959, 960, 961, 1440, 1920, 5000}){
        CHECK(secondsToTick(chart, tickToSeconds(chart, tick)) == doctest::Approx(tick));
    }
    CHECK(secondsToTick(chart, 1.5) == doctest::Approx(1080.0)); // 1 s after the offset is 960, then 0.25 s at 60 bpm = 120 ticks
    CHECK(secondsToTick(chart, 0.0) == doctest::Approx(-240.0)); // before the offset: back at the first tempo
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
        {"newer version",       "version 3\n" + HEADER.substr(10) + TRACK, "newer than this build supports"},
        {"time without slash",  HEADER + "time 0 3 4\n" + TRACK,   "expected: time <tick> <beats>/<beat unit>"},
        {"odd beat unit",       HEADER + "time 0 7/6\n" + TRACK,   "beat unit of 1, 2, 4, 8, 16 or 32"},
        {"no time at tick 0",   HEADER + "time 1920 3/4\n" + TRACK, "needs a time signature at tick 0"},
        {"time mid-bar",        HEADER + "time 0 4/4\ntime 960 3/4\n" + TRACK, "time signature at tick 960 isn't on a bar line"},
        {"unknown key",         HEADER + "key 0 G# major\n" + TRACK, "unknown key 'G# major'"},
        {"unknown hit sound",   HEADER + TRACK + "hit_sound kazoo\n", "unknown hit sound 'kazoo'"},
        {"trim ends before it starts", HEADER + "trim 12 8\n" + TRACK, "trim needs a start >= 0 and an end after it"},
        {"key mid-bar",         HEADER + "key 0 C major\nkey 480 G major\n" + TRACK, "key at tick 480 isn't on a bar line"},
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
    original.timeSignatures = {{0, 4, 4}, {3840, 6, 8}};
    original.keys = {{0, {-3, false}}, {3840, {3, true}}}; // Eb major, then F# minor
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
    REQUIRE(loaded.timeSignatures.size() == 2);
    CHECK(loaded.timeSignatures[1].tick == 3840);
    CHECK(loaded.timeSignatures[1].beats == 6);
    CHECK(loaded.timeSignatures[1].beatUnit == 8);
    REQUIRE(loaded.keys.size() == 2);
    CHECK(loaded.keys[0].key.fifths == -3);
    CHECK(loaded.keys[1].key.fifths == 3);
    CHECK(loaded.keys[1].key.minor);
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

TEST_CASE("version 1 charts are 4/4 in C major"){
    Chart chart;
    std::string error;
    REQUIRE_MESSAGE(loadChart(writeTemp("v1.chart", HEADER + TRACK + "n 960 0 0\n"), chart, error), error);
    REQUIRE(chart.timeSignatures.size() == 1);
    CHECK(chart.timeSignatures[0].beats == 4);
    CHECK(chart.timeSignatures[0].beatUnit == 4);
    REQUIRE(chart.keys.size() == 1);
    CHECK(chart.keys[0].key.fifths == 0);
    CHECK(barTicks(chart) == std::vector<int>{0, 1920, 3840, 5760, 7680, 9600}); // end 9600 is on a bar line
}

TEST_CASE("bars follow the time signatures"){
    Chart chart;
    std::string error;
    // 2 bars of 4/4, then 3/4 (1440 ticks a bar), then 6/8 (six eighths: also 1440 ticks)
    REQUIRE_MESSAGE(loadChart(writeTemp("meters.chart",
        "version 2\nresolution 480\nend 9000\ntempo 0 120\ntime 0 4/4\ntime 3840 3/4\ntime 6720 6/8\nkey 0 D major\n" + TRACK),
        chart, error), error);
    CHECK(barTicks(chart) == std::vector<int>{0, 1920, 3840, 5280, 6720, 8160});
    CHECK(timeSignatureAt(chart, 0).beats == 4);
    CHECK(timeSignatureAt(chart, 3839).beats == 4);
    CHECK(timeSignatureAt(chart, 3840).beats == 3);
    CHECK(timeSignatureAt(chart, 8000).beatUnit == 8);
    CHECK(ticksPerBar(chart, {0, 6, 8}) == 1440);
    CHECK(barNumberAt(chart, 0) == 0);
    CHECK(barNumberAt(chart, 3839) == 1);
    CHECK(barNumberAt(chart, 3840) == 2);  // the first 3/4 bar
    CHECK(barNumberAt(chart, 6719) == 3);
    CHECK(barNumberAt(chart, 20000) == 13); // past the end, 6/8 carries on: bar 4 + (20000 - 6720) / 1440
    CHECK(barStartTick(chart, 3) == 5280);
    CHECK(barStartTick(chart, 11) == 6720 + 7 * 1440);
    CHECK(ticksPerBar(chart, {0, 2, 2}) == 1920); // cut time: two half notes
    CHECK(chart.keys[0].key.fifths == 2);
}

TEST_CASE("keys parts: notes as pitches, saved and read back"){
    std::filesystem::path path = std::filesystem::temp_directory_path() / "lahn_tests" / "keys.chart";
    std::filesystem::create_directories(path.parent_path());
    std::ofstream(path, std::ios::binary) << "version 2\ntitle Keys\nresolution 480\nend 1920\ntempo 0 100\n"
                                             "track keys Piano\nn 480 64 240\nn 0 60\nn 0 67 480\n";
    Chart chart;
    std::string error;
    REQUIRE_MESSAGE(loadChart(path.string(), chart, error), error);
    CHECK(chart.frettedTracks.empty());   // a piano-only song is a song
    REQUIRE(chart.keysTracks.size() == 1);
    CHECK(chart.keysTracks[0].name == "Piano");
    const std::vector<KeysNote>& notes = chart.keysTracks[0].notes;
    REQUIRE(notes.size() == 3);
    CHECK(notes[0].pitch == 60);          // sorted by tick, then pitch
    CHECK(notes[1].pitch == 67);
    CHECK(notes[1].duration == 480);
    CHECK(notes[2].tick == 480);
    CHECK(partCount(chart) == 1);
    CHECK(isKeysPart(chart, 0));
    CHECK(partName(chart, 0) == "Piano");

    REQUIRE_MESSAGE(saveChart(path.string(), chart, error), error);
    Chart again;
    REQUIRE_MESSAGE(loadChart(path.string(), again, error), error);
    REQUIRE(again.keysTracks.size() == 1);
    CHECK(again.keysTracks[0].notes.size() == 3);
    CHECK(again.keysTracks[0].notes[1].duration == 480);

    std::ofstream(path, std::ios::binary) << "version 2\nresolution 480\nend 1920\ntempo 0 100\ntrack keys P\nn 0 60\nn 0 60\n";
    CHECK_FALSE(loadChart(path.string(), again, error)); // the same note twice
    std::ofstream(path, std::ios::binary) << "version 2\nresolution 480\nend 1920\ntempo 0 100\ntrack keys P\nn 0 200\n";
    CHECK_FALSE(loadChart(path.string(), again, error)); // no such pitch
}

TEST_CASE("a trimmed song: its trim saved and read back, the notes outside it left out"){
    Chart chart;
    std::string error;
    // 120 BPM, 480 ticks a beat: a beat is half a second, and with the offset tick 0 is 1 s into the audio
    REQUIRE(loadChart(writeTemp("trim.chart", "version 2\nresolution 480\noffset 1\nend 7680\ntempo 0 120\ntrim 2 4.5\n"
                                              "track guitar Lead\ntuning 40 45 50 55 59 64\n"
                                              "n 0 0 0\nn 960 0 1\nn 1920 0 2\nn 2880 0 3\nn 3360 0 4\nn 3840 0 5\n"
                                              "track keys Piano\nn 480 60\nn 2400 62\n"), chart, error));
    CHECK(chart.trimStart == doctest::Approx(2.0));
    CHECK(chart.trimEnd == doctest::Approx(4.5));

    std::string path = tempPath("trim-saved.chart");
    REQUIRE(saveChart(path, chart, error));
    Chart again;
    REQUIRE(loadChart(path, again, error));
    CHECK(again.trimStart == doctest::Approx(2.0));
    CHECK(again.trimEnd == doctest::Approx(4.5));

    // The song is the audio from 2 s to 4.5 s: ticks 960 (2 s) to just before 3360 (4.5 s)
    dropTrimmedNotes(again);
    REQUIRE(again.frettedTracks[0].notes.size() == 3);
    CHECK(again.frettedTracks[0].notes.front().tick == 960);
    CHECK(again.frettedTracks[0].notes.back().tick == 2880);
    REQUIRE(again.keysTracks[0].notes.size() == 1);
    CHECK(again.keysTracks[0].notes[0].tick == 2400);

    // Not trimmed: nothing is written about it, and nothing left out
    chart.trimStart = chart.trimEnd = 0.0;
    REQUIRE(saveChart(path, chart, error));
    Chart whole;
    REQUIRE(loadChart(path, whole, error));
    CHECK(whole.trimStart == 0.0);
    dropTrimmedNotes(whole);
    CHECK(whole.frettedTracks[0].notes.size() == 6);
}

TEST_CASE("parts brought in from another chart keep their bars and beats"){
    // The song: 480 ticks to the beat, 120 beats a minute, four bars, a guitar part of its own
    Chart song{};
    song.resolution = 480;
    song.offset = 1.25;
    song.endTick = 4 * 4 * 480;
    song.tempoMap = { { 0, 120.0 } };
    song.timeSignatures = { { 0, 4, 4 } };
    song.keys = { { 0, KeySignature{} } };
    FrettedTrack guitar;
    guitar.name = "Guitar";
    guitar.tuning = { 40, 45, 50, 55, 59, 64 };
    song.frettedTracks = { guitar };
    // The tab: 960 ticks to the beat, 90 beats a minute in 3/4, a bass and a guitar, six bars
    Chart tab{};
    tab.resolution = 960;
    tab.endTick = 6 * 3 * 960;
    tab.tempoMap = { { 0, 90.0 }, { 3 * 960, 100.0 } };
    tab.timeSignatures = { { 0, 3, 4 } };
    tab.keys = { { 0, KeySignature{ 1, false } } };
    FrettedTrack bass;
    bass.type = InstrumentType::Bass;
    bass.name = "Bass";
    bass.tuning = { 28, 33, 38, 43 };
    bass.notes = { { 0, 0, 3, 960 }, { 1440, 1, 5, 480 }, { 17 * 960, 2, 0, 0 } }; // the last in the tab's sixth bar
    FrettedTrack lead;
    lead.name = "Lead";
    lead.tuning = guitar.tuning;
    tab.frettedTracks = { bass, lead };

    SUBCASE("the parts alone: on the song's own bars"){
        importParts(song, tab, { 0 }, false);
        REQUIRE(song.frettedTracks.size() == 2);
        const FrettedTrack& added = song.frettedTracks[1];
        CHECK(added.name == "Bass");
        CHECK(added.type == InstrumentType::Bass);
        REQUIRE(added.notes.size() == 3);
        CHECK(added.notes[0].duration == 480);          // a beat stays a beat
        CHECK(added.notes[1].tick == 720);              // and a beat and a half, a beat and a half
        CHECK(added.notes[1].duration == 240);
        CHECK(song.tempoMap.size() == 1);               // the song's own tempo
        CHECK(song.tempoMap[0].bpm == 120.0);
        CHECK(song.offset == 1.25);
        CHECK(song.endTick == 5 * 4 * 480);             // longer: to the end of the bar the last note is in (beat 17)
    }
    SUBCASE("with its bars: the tab's tempos, time signature and key"){
        importParts(song, tab, { 0, 1, 7 }, true);      // a part it doesn't have is passed over
        REQUIRE(song.frettedTracks.size() == 3);
        REQUIRE(song.tempoMap.size() == 2);
        CHECK(song.tempoMap[1].tick == 3 * 480);
        CHECK(song.tempoMap[1].bpm == 100.0);
        CHECK(song.timeSignatures[0].beats == 3);
        CHECK(song.keys[0].key.fifths == 1);
        CHECK(song.offset == 1.25);                     // a tab has no audio: where bar one starts is the song's to say
        CHECK(song.endTick == 6 * 3 * 480);             // the tab's six bars of 3/4, not the song's four of 4/4
    }
    SUBCASE("brought in twice: the second of a name gets a number"){
        importParts(song, tab, { 0, 1 }, false);
        importParts(song, tab, { 0 }, false);
        importParts(song, tab, { 0 }, false);
        REQUIRE(song.frettedTracks.size() == 5);
        CHECK(song.frettedTracks[1].name == "Bass");
        CHECK(song.frettedTracks[3].name == "Bass 2");
        CHECK(song.frettedTracks[4].name == "Bass 3");
    }
    SUBCASE("with its bars, a note of the song's own past the tab's end keeps its bar"){
        song.frettedTracks[0].notes = { { 7 * 3 * 480 + 240, 0, 0, 0 } }; // in what becomes the eighth bar of 3/4
        importParts(song, tab, { 0 }, true);
        CHECK(song.endTick == 8 * 3 * 480);
    }
    SUBCASE("from a chart lined up with a recording: where its first bar starts comes too"){
        tab.audioFile = "audio.mp3";
        tab.offset = 0.4;
        importParts(song, tab, { 0 }, true);
        CHECK(song.offset == 0.4);
    }
}

TEST_CASE("a song's video: its file and where the audio starts in it, saved and read back"){
    Chart chart;
    std::string error;
    REQUIRE_MESSAGE(loadChart(writeTemp("video.chart", "version 2\nvideo clip.mpg\nvideo_offset 1.5\nresolution 480\noffset 0\nend 1920\ntempo 0 120\n"
                                                       "track bass Bass\ntuning 28 33 38 43\nn 0 0 3\n"), chart, error), error);
    CHECK(chart.videoFile == "clip.mpg");
    CHECK(chart.videoOffset == doctest::Approx(1.5));
    std::string path = writeTemp("video-saved.chart", "");
    REQUIRE_MESSAGE(saveChart(path, chart, error), error);
    Chart again;
    REQUIRE_MESSAGE(loadChart(path, again, error), error);
    CHECK(again.videoFile == "clip.mpg");
    CHECK(again.videoOffset == doctest::Approx(1.5));
    // A song without one says nothing of it
    again.videoFile.clear();
    REQUIRE_MESSAGE(saveChart(path, again, error), error);
    REQUIRE_MESSAGE(loadChart(path, chart, error), error);
    CHECK(chart.videoFile.empty());
    CHECK(chart.videoOffset == 0.0);
}

TEST_CASE("a part's hit sound: saved and read back, and its own instrument says nothing"){
    Chart chart;
    std::string error;
    REQUIRE_MESSAGE(loadChart(writeTemp("hitsound.chart", HEADER + TRACK + "hit_sound keys\nn 0 0 3\n" + "track bass Low\ntuning 28 33 38 43\n"), chart, error), error);
    REQUIRE(chart.frettedTracks.size() == 2);
    CHECK(chart.frettedTracks[0].hitSound == "keys");
    CHECK(chart.frettedTracks[1].hitSound.empty());
    std::string path = writeTemp("hitsound-saved.chart", "");
    REQUIRE_MESSAGE(saveChart(path, chart, error), error);
    Chart again;
    REQUIRE_MESSAGE(loadChart(path, again, error), error);
    CHECK(again.frettedTracks[0].hitSound == "keys");
    CHECK(again.frettedTracks[1].hitSound.empty());
    std::ifstream in(path);
    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    CHECK(text.find("hit_sound") == text.rfind("hit_sound")); // written once: the bass part says nothing
}
