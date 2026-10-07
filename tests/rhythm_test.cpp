#include "doctest/doctest.h"

#include "core/exercisefile.h"
#include "core/rhythm.h"

#include <filesystem>
#include <fstream>

#include <cmath>

TEST_CASE("rhythms are built from the cells asked for, a bar at a time"){
    RhythmConfig config;
    config.cells = { "eighths", "offbeat", "rest" };
    config.bars = 3;
    config.beatsPerBar = 3;
    std::mt19937 rng(11);
    for (int pass = 0; pass < 50; pass++){
        std::vector<DrillNote> notes = buildRhythm(config, rng);
        REQUIRE_FALSE(notes.empty());
        std::vector<int> perBar(config.bars, 0);
        double previous = -1.0;
        for (const DrillNote& note : notes){
            CHECK(note.beat > previous); // in time order
            previous = note.beat;
            double inBeat = note.beat - std::floor(note.beat);
            CHECK((inBeat == 0.0 || inBeat == 0.5)); // only what eighths and an offbeat eighth can give
            CHECK(note.stringIndex == 4);
            CHECK(note.pitch == 59);                 // the open B string: written on the middle line
            int bar = (int)(note.beat / config.beatsPerBar);
            REQUIRE(bar < config.bars);
            perBar[bar]++;
        }
        for (int count : perBar) CHECK(count > 0); // every bar has something to play
    }
}

TEST_CASE("a rhythm is played on the string written nearest the middle line"){
    CHECK(rhythmString({ 40, 45, 50, 55, 59, 64 }) == 4); // a guitar's B string
    CHECK(rhythmString({ 38, 45, 50, 55, 59, 64 }) == 4); // drop D changes nothing
    CHECK(rhythmString({ 64 }) == 0);                     // one string: that one
}

TEST_CASE("every cell fits in its beat, and names are found"){
    for (const RhythmCell& cell : rhythmCells()){
        for (double onset : cell.onsets) CHECK((onset >= 0.0 && onset < 1.0));
        CHECK(findRhythmCell(cell.name) == &cell);
    }
    CHECK(findRhythmCell("waltz") == nullptr);
}

TEST_CASE("the same seed gives the same rhythm, another seed another one"){
    RhythmConfig config;
    config.cells = { "quarter", "eighths", "sixteenths", "rest" };
    config.bars = 4;
    std::mt19937 a(3), b(3), c(4);
    std::vector<DrillNote> first = buildRhythm(config, a), again = buildRhythm(config, b), other = buildRhythm(config, c);
    REQUIRE(first.size() == again.size());
    for (size_t i = 0; i < first.size(); i++) CHECK(first[i].beat == again[i].beat);
    bool different = first.size() != other.size();
    for (size_t i = 0; !different && i < first.size(); i++) different = first[i].beat != other[i].beat;
    CHECK(different);
}

TEST_CASE("rhythm exercise files"){
    std::filesystem::path path = std::filesystem::temp_directory_path() / "hardthz_tests" / "exercises" / "rhythm.exercise";
    std::filesystem::create_directories(path.parent_path());
    std::ofstream(path, std::ios::binary) << "version 1\ntype rhythm\ntitle Offbeats\ncells eighths offbeat\nbars 4\ntime 3\ntempo 70 120 5\npass 80\n";
    ExerciseFile file;
    std::string error;
    REQUIRE_MESSAGE(loadExerciseFile(path.string(), file, error), error);
    CHECK(file.type == ExerciseType::Rhythm);
    CHECK(file.rhythm.cells == std::vector<std::string>{"eighths", "offbeat"});
    CHECK(file.rhythm.bars == 4);
    CHECK(file.rhythm.beatsPerBar == 3);
    CHECK(file.rhythm.tempo.startTempo == 70);
    CHECK(file.rhythm.tempo.passPercent == 80);

    std::ofstream(path, std::ios::binary) << "version 1\ntype rhythm\ntitle Bad\ncells eighths waltz\n";
    CHECK_FALSE(loadExerciseFile(path.string(), file, error));
    CHECK(error.find("waltz") != std::string::npos);
}
