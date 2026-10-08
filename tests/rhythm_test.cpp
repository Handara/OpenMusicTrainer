#include "doctest/doctest.h"

#include "core/drill.h"
#include "core/exercisefile.h"
#include "core/rhythm.h"
#include "core/score.h"

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

TEST_CASE("each note lasts until its cell's next, or its end: rests written as rests, longer cells where they read"){
    RhythmConfig config;
    config.cells = { "quarter", "rest", "half", "whole", "dotted_quarter" };
    config.bars = 40;
    std::mt19937 rng(5);
    const std::vector<DrillNote> notes = buildRhythm(config, rng);
    bool half = false, whole = false;
    for (size_t i = 0; i < notes.size(); i++){
        const DrillNote& note = notes[i];
        CHECK(note.length > 0.0);
        if (i + 1 < notes.size()) CHECK(note.beat + note.length <= notes[i + 1].beat + 1e-9); // never over the next
        const double inBar = std::fmod(note.beat, 4.0);
        if (note.length == 2.0){ half = true; CHECK((inBar == 0.0 || inBar == 2.0)); }   // a half: on beat 1 or 3
        if (note.length == 4.0){ whole = true; CHECK(inBar == 0.0); }                    // a whole: the downbeat
        CHECK(std::floor(note.beat / 4.0) == std::floor((note.beat + note.length - 1e-9) / 4.0)); // inside its bar
    }
    CHECK(half);
    CHECK(whole);

    // A quarter then a beat's rest: written a quarter and a quarter rest, not a half note
    std::vector<DrillNote> written = { { 0.0, 0, 0, 64 }, { 2.0, 0, 0, 64 }, { 3.0, 0, 0, 64 } };
    written[0].length = written[1].length = written[2].length = 1.0;
    Chart chart = drillChart(written, { 0 }, KeySignature{}, 4);
    const Score score = buildScore(chart, chart.frettedTracks[0]);
    REQUIRE(score.events.size() == 4);
    CHECK_FALSE(score.events[0].rest);
    CHECK(score.events[0].value == NoteValue::Quarter);
    CHECK(score.events[1].rest);
    CHECK(score.events[1].value == NoteValue::Quarter);
}

TEST_CASE("a rhythm is played on the string written nearest the middle line"){
    CHECK(rhythmString({ 40, 45, 50, 55, 59, 64 }) == 4); // a guitar's B string
    CHECK(rhythmString({ 38, 45, 50, 55, 59, 64 }) == 4); // drop D changes nothing
    CHECK(rhythmString({ 64 }) == 0);                     // one string: that one
}

TEST_CASE("every cell fits in its beat, and names are found"){
    for (const RhythmCell& cell : rhythmCells()){
        CHECK((cell.beats >= 1 && cell.beats <= 4));
        for (double onset : cell.onsets) CHECK((onset >= 0.0 && onset < cell.beats));
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
    std::filesystem::path path = std::filesystem::temp_directory_path() / "lahn_tests" / "exercises" / "rhythm.exercise";
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
