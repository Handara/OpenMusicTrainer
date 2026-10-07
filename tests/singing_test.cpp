#include "doctest/doctest.h"

#include "core/exercisefile.h"
#include "core/singing.h"

#include <filesystem>
#include <fstream>

TEST_CASE("notes to sing stay in the range and don't repeat"){
    SingingConfig config; // C3 to G4, naturals
    std::mt19937 rng(2);
    int last = -1;
    for (int i = 0; i < 300; i++){
        int note = nextSingingNote(config, rng, last);
        CHECK(note >= config.lowest);
        CHECK(note <= config.highest);
        int pitchClass = note % 12;
        CHECK((pitchClass != 1 && pitchClass != 3 && pitchClass != 6 && pitchClass != 8 && pitchClass != 10));
        CHECK(note != last);
        last = note;
    }
}

TEST_CASE("how far off a sung note is"){
    SingingConfig config;
    CHECK(singingErrorCents(config, 60, 60.2f) == doctest::Approx(20.0f));
    CHECK(singingErrorCents(config, 60, 47.9f) == doctest::Approx(-10.0f).epsilon(0.001)); // an octave down, a little flat: fine
    config.anyOctave = false;
    CHECK(singingErrorCents(config, 60, 47.9f) == doctest::Approx(-1210.0f)); // this time the octave counts
}

TEST_CASE("a note counts once it's held in tune long enough, without a break"){
    SingingConfig config; // 30 cents, one second
    PitchHold hold;
    const double frame = 0.1;
    for (int i = 0; i < 5; i++) CHECK_FALSE(holdPitch(hold, config, 57, 57.1f, frame)); // half a second in tune
    CHECK_FALSE(holdPitch(hold, config, 57, 57.6f, frame));                          // a wobble: starts over
    CHECK(hold.inTuneFor == 0.0);
    CHECK_FALSE(holdPitch(hold, config, 57, -1.0f, frame));                          // silence: still over
    bool held = false;
    for (int i = 0; i < 10 && !held; i++) held = holdPitch(hold, config, 57, 56.8f, frame); // 20 cents flat: in tune
    CHECK(held);
}

TEST_CASE("singing exercise files"){
    std::filesystem::path path = std::filesystem::temp_directory_path() / "lahn_tests" / "exercises" / "sing.exercise";
    std::filesystem::create_directories(path.parent_path());
    std::ofstream(path, std::ios::binary) << "version 1\ntype singing\ntitle Low voice\nrange 40 55\nnotes all\noctave exact\ntolerance 20\nhold 1.5\n";
    ExerciseFile file;
    std::string error;
    REQUIRE_MESSAGE(loadExerciseFile(path.string(), file, error), error);
    CHECK(file.type == ExerciseType::Singing);
    CHECK(file.singing.lowest == 40);
    CHECK_FALSE(file.singing.naturalsOnly);
    CHECK_FALSE(file.singing.anyOctave);
    CHECK(file.singing.toleranceCents == doctest::Approx(20.0f));
    CHECK(file.singing.holdSeconds == doctest::Approx(1.5));

    std::ofstream(path, std::ios::binary) << "version 1\ntype singing\ntitle Bad\ntolerance 90\n";
    CHECK_FALSE(loadExerciseFile(path.string(), file, error));
}
