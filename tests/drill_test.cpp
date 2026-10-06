#include "doctest/doctest.h"

#include "core/drill.h"

#include <filesystem>

TEST_CASE("a G major drill, up and down, in eighth notes"){
    ScaleDrillConfig config; // G major, 2 octaves, 2nd position, up and down, 2 notes per beat
    std::vector<DrillNote> notes;
    std::string error;
    REQUIRE_MESSAGE(buildScaleDrill(config, notes, error), error);
    REQUIRE(notes.size() == 29);             // 15 notes up, 14 back down (the top G isn't repeated)
    CHECK(notes.front().pitch == 43);        // starts on G2...
    CHECK(notes.front().fret == 3);          // ...3rd fret, low E
    CHECK(notes[14].pitch == 67);            // turns around on G4
    CHECK(notes.back().pitch == 43);         // ends where it started
    CHECK(notes[1].beat == doctest::Approx(0.5)); // eighth notes
    CHECK(notes.back().beat == doctest::Approx(14.0));
}

TEST_CASE("direction, fingering and scale choices"){
    ScaleDrillConfig config;
    std::vector<DrillNote> notes;
    std::string error;
    config.direction = DrillDirection::Down;
    config.octaves = 1;
    REQUIRE(buildScaleDrill(config, notes, error));
    CHECK(notes.size() == 8);
    CHECK(notes.front().pitch == 55); // G3, coming down
    CHECK(notes.back().pitch == 43);

    config.scale = "minor_pentatonic";
    config.rootPitchClass = 9; // A
    config.direction = DrillDirection::Up;
    config.octaves = 2;
    REQUIRE_MESSAGE(buildScaleDrill(config, notes, error), error);
    CHECK(notes.front().fret == 5); // A on the 5th fret: position picked from the root automatically
    CHECK(notes.size() == 11);

    config.scale = "kazoo";
    CHECK_FALSE(buildScaleDrill(config, notes, error));
    CHECK(error.find("unknown scale") != std::string::npos);
}

TEST_CASE("the tempo ramp"){
    ScaleDrillConfig config; // start 60, step 4, max 160, pass at 90%
    DrillProgress progress;
    CHECK(drillTempo(config.tempo, progress) == 60);

    DrillPassOutcome clean = finishDrillPass(config.tempo, progress, 60, 95.0f);
    CHECK(clean.clean);
    CHECK(clean.newBest);
    CHECK(clean.nextTempo == 64);
    CHECK(progress.bestCleanTempo == 60);

    DrillPassOutcome close = finishDrillPass(config.tempo, progress, 64, 80.0f); // not clean, not bad: stay
    CHECK_FALSE(close.clean);
    CHECK(close.nextTempo == 64);

    DrillPassOutcome bad = finishDrillPass(config.tempo, progress, 64, 30.0f);   // struggling: slow down
    CHECK(bad.nextTempo == 60);
    CHECK(progress.bestCleanTempo == 60); // the best is kept

    progress.tempo = 158;
    CHECK(finishDrillPass(config.tempo, progress, 158, 100.0f).nextTempo == 160); // never past the max
    CHECK(progress.passes == 4);
    CHECK(progress.cleanPasses == 2);
}

TEST_CASE("drill progress survives a save and load"){
    DrillProgress original{84, 80, 12, 7};
    std::filesystem::path dir = std::filesystem::temp_directory_path() / "lahn_tests";
    std::filesystem::create_directories(dir);
    std::string path = (dir / "drill.txt").string(), error;
    REQUIRE(saveDrillProgress(path, original, error));
    DrillProgress loaded = loadDrillProgress(path);
    CHECK(loaded.tempo == 84);
    CHECK(loaded.bestCleanTempo == 80);
    CHECK(loaded.passes == 12);
    CHECK(loaded.cleanPasses == 7);
}

TEST_CASE("a drill written as a chart"){
    ScaleDrillConfig config; // G major, eighth notes
    std::vector<DrillNote> notes;
    std::string error;
    REQUIRE(buildScaleDrill(config, notes, error));
    Chart chart = drillChart(notes, config.tuning, scaleDrillKey(config));
    REQUIRE(chart.frettedTracks.size() == 1);
    const std::vector<FrettedNote>& chartNotes = chart.frettedTracks[0].notes;
    REQUIRE(chartNotes.size() == notes.size());
    CHECK(chartNotes[1].tick == 240);
    CHECK(chartNotes.back().tick == 14 * 480);
    CHECK(chart.endTick == 16 * 480);         // the last note is in bar 4: the chart ends with it
    CHECK(chart.keys[0].key.fifths == 1);     // one sharp

    config.notesPerBeat = 3;                  // triplets land exactly on the ticks
    REQUIRE(buildScaleDrill(config, notes, error));
    CHECK(drillChart(notes, config.tuning, scaleDrillKey(config)).frettedTracks[0].notes[1].tick == 160);
}

TEST_CASE("a drill's tempo so far stays within its tempos, should they change"){
    DrillTempo rules{ 100, 120, 5, 80 };
    DrillProgress progress;
    CHECK(drillTempo(rules, progress) == 100);
    progress.tempo = 55;  // played when it started at 50
    CHECK(drillTempo(rules, progress) == 100);
    progress.tempo = 110;
    CHECK(drillTempo(rules, progress) == 110);
    progress.tempo = 150;
    CHECK(drillTempo(rules, progress) == 120);
}
