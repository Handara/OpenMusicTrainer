#include "doctest/doctest.h"

#include "core/exercisefile.h"
#include "core/fretboard.h"

#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

const int C = 0, E = 4, A = 9;

TEST_CASE("where a note is on a string"){
    FretboardConfig config; // standard tuning, frets 0 to 12
    CHECK(fretboardAnswers(config, {1, C}) == std::vector<int>{3});      // C on the A string: 3rd fret
    CHECK(fretboardAnswers(config, {0, E}) == std::vector<int>{0, 12});  // E on the low E: open, and the 12th fret
    CHECK(fretboardAnswers(config, {5, A}) == std::vector<int>{5});      // A on the high E: 5th fret
    config.lowestFret = 5;
    CHECK(fretboardAnswers(config, {0, E}) == std::vector<int>{12});     // the open string is out of range now
}

TEST_CASE("answers by clicking a fret or playing a pitch"){
    FretboardConfig config;
    FretboardQuestion cOnA{1, C};
    CHECK(fretIsRight(config, cOnA, 3));
    CHECK_FALSE(fretIsRight(config, cOnA, 4));
    CHECK(pitchIsRight(config, cOnA, 48));       // C3: the A string's 3rd fret
    CHECK(pitchIsRight(config, cOnA, 45 + 3));   // the same pitch however it's reached (the low E's 8th fret too)
    CHECK_FALSE(pitchIsRight(config, cOnA, 60)); // C4: the right note, but not on the A string in these frets
}

TEST_CASE("questions stay inside the rules and don't repeat"){
    FretboardTrainer trainer;
    trainer.config.strings = {1};         // the A string only
    trainer.config.naturalsOnly = true;
    trainer.rng.seed(7);
    FretboardQuestion previous{-1, -1};
    for (int i = 0; i < 200; i++){
        FretboardQuestion question = nextFretboardQuestion(trainer);
        CHECK(question.stringIndex == 1);
        bool natural = question.pitchClass == 0 || question.pitchClass == 2 || question.pitchClass == 4 || question.pitchClass == 5
                    || question.pitchClass == 7 || question.pitchClass == 9 || question.pitchClass == 11;
        CHECK(natural);
        CHECK_FALSE(fretboardAnswers(trainer.config, question).empty());
        CHECK_FALSE((question.stringIndex == previous.stringIndex && question.pitchClass == previous.pitchClass));
        previous = question;
    }
}

TEST_CASE("the streak and progress"){
    FretboardTrainer trainer;
    recordQuizAnswer(trainer.progress, trainer.streak, true);
    recordQuizAnswer(trainer.progress, trainer.streak, true);
    recordQuizAnswer(trainer.progress, trainer.streak, false);
    recordQuizAnswer(trainer.progress, trainer.streak, true);
    CHECK(trainer.streak == 1);
    CHECK(trainer.progress.bestStreak == 2);
    CHECK(trainer.progress.asked == 4);
    CHECK(trainer.progress.correct == 3);

    fs::path path = fs::temp_directory_path() / "lahn_tests" / "fretboard_progress.txt";
    fs::create_directories(path.parent_path());
    std::string error;
    REQUIRE_MESSAGE(saveQuizProgress(path.string(), trainer.progress, error), error);
    QuizProgress loaded = loadQuizProgress(path.string());
    CHECK(loaded.asked == 4);
    CHECK(loaded.correct == 3);
    CHECK(loaded.bestStreak == 2);
}

static std::string writeExercise(const std::string& name, const std::string& content){
    fs::path path = fs::temp_directory_path() / "lahn_tests" / "exercises" / name;
    fs::create_directories(path.parent_path());
    std::ofstream(path, std::ios::binary) << content;
    return path.string();
}

TEST_CASE("fretboard exercise files"){
    ExerciseFile file;
    std::string error;
    REQUIRE_MESSAGE(loadExerciseFile(writeExercise("low.exercise",
        "version 1\ntype fretboard\ntitle Low strings\nstrings 1 2\nfrets 0 5\nnotes all\n"), file, error), error);
    CHECK(file.type == ExerciseType::Fretboard);
    CHECK(file.fretboard.strings == std::vector<int>{0, 1}); // written from 1, kept from 0
    CHECK(file.fretboard.highestFret == 5);
    CHECK_FALSE(file.fretboard.naturalsOnly);

    // A 4-string bass: string 4 exists, 5 doesn't, whatever order the lines come in
    CHECK(loadExerciseFile(writeExercise("bass.exercise",
        "version 1\ntype fretboard\ntitle Bass\nstrings 4\ntuning 28 33 38 43\n"), file, error));
    CHECK_FALSE(loadExerciseFile(writeExercise("bad.exercise",
        "version 1\ntype fretboard\ntitle Bass\nstrings 5\ntuning 28 33 38 43\n"), file, error));
    CHECK(error.find(":4:") != std::string::npos); // the strings line
    CHECK_FALSE(loadExerciseFile(writeExercise("frets.exercise",
        "version 1\ntype fretboard\ntitle Frets\nfrets 12 3\n"), file, error));
}
