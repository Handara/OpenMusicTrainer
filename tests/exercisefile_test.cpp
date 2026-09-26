#include "doctest/doctest.h"

#include "core/exercisefile.h"

#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

static fs::path testDir(){
    fs::path dir = fs::temp_directory_path() / "omt_tests" / "exercises";
    fs::create_directories(dir);
    return dir;
}

static std::string writeExercise(const std::string& name, const std::string& content){
    fs::path path = testDir() / name;
    std::ofstream(path, std::ios::binary) << content;
    return path.string();
}

static const std::string HEADER = "version 1\ntype intervals\ntitle Test\n";

TEST_CASE("a full exercise file loads"){
    std::string path = writeExercise("thirds.exercise",
        "# OpenMusicTrainer exercise\r\n" + HEADER +
        "category Ear training\nauthor Someone\ndescription Major or minor?\n"
        "direction down\nintervals 4 3 7\nstart 2\nunlock 4 5\nrange 50 60\ngap 1.2\n");
    ExerciseFile file;
    std::string error;
    REQUIRE_MESSAGE(loadExerciseFile(path, file, error), error);
    CHECK(file.type == ExerciseType::Intervals);
    CHECK(file.title == "Test");
    CHECK(file.category == "Ear training");
    CHECK(file.author == "Someone");
    CHECK(file.description == "Major or minor?");
    CHECK(file.intervals.direction == IntervalDirection::Descending);
    CHECK(file.intervals.pool == std::vector<int>{4, 3, 7});
    CHECK(file.intervals.startCount == 2);
    CHECK(file.intervals.unlockCorrect == 4);
    CHECK(file.intervals.unlockWindow == 5);
    CHECK(file.intervals.lowestRoot == 50);
    CHECK(file.intervals.highestRoot == 60);
    CHECK(file.intervals.gapSeconds == doctest::Approx(1.2f));
}

TEST_CASE("everything optional falls back to the classic course"){
    ExerciseFile file;
    std::string error;
    REQUIRE_MESSAGE(loadExerciseFile(writeExercise("minimal.exercise", HEADER), file, error), error);
    CHECK(file.category == "Other");
    CHECK(file.intervals.pool.size() == 12);
    CHECK(file.intervals.startCount == 2);
    CHECK(file.intervals.direction == IntervalDirection::Ascending);

    // Two intervals and no 'start': both unlocked from the beginning
    REQUIRE(loadExerciseFile(writeExercise("two.exercise", HEADER + "intervals 12 7\n"), file, error));
    CHECK(file.intervals.startCount == 2);
}

TEST_CASE("broken exercise files are rejected with a clear message"){
    struct Case { const char* name; std::string content; const char* expected; };
    const Case cases[] = {
        {"no version",        "type intervals\ntitle T\n",              "missing 'version'"},
        {"newer version",     "version 2\ntype intervals\ntitle T\n",   "newer than this build supports"},
        {"no type",           "version 1\ntitle T\n",                   "missing 'type'"},
        {"unknown type",      "version 1\ntype karaoke\ntitle T\n",     "unknown exercise type 'karaoke'"},
        {"no title",          "version 1\ntype intervals\n",            "missing 'title'"},
        {"bad direction",     HEADER + "direction sideways\n",          "direction must be up, down or together"},
        {"interval too big",  HEADER + "intervals 4 13\n",              "1 (minor 2nd) to 12 (octave)"},
        {"duplicate",         HEADER + "intervals 4 3 4\n",             "interval 4 is listed twice"},
        {"one interval",      HEADER + "intervals 4\n",                 "at least 2 intervals"},
        {"unlock impossible", HEADER + "unlock 11 10\n",                "right answers <= out of"},
        {"range reversed",    HEADER + "range 70 50\n",                 "lowest first"},
        {"range too high",    HEADER + "range 50 120\n",                "past MIDI 127"},
        {"start too big",     HEADER + "intervals 4 3\nstart 3\n",      "'start' is more than"},
        {"unknown setting",   HEADER + "tempo 120\n",                   "unknown setting 'tempo'"},
        {"trailing text",     HEADER + "gap 0.5 fast\n",                "unexpected text after 'gap'"},
    };
    for (const Case& c : cases){
        SUBCASE(c.name){
            ExerciseFile file;
            std::string error;
            CHECK_FALSE(loadExerciseFile(writeExercise("bad.exercise", c.content), file, error));
            CHECK_MESSAGE(error.find(c.expected) != std::string::npos, error);
        }
    }
}

TEST_CASE("scanning a folder finds exercise files, broken ones included"){
    fs::path dir = testDir() / "scan";
    fs::remove_all(dir);
    fs::create_directories(dir);
    std::ofstream(dir / "b.exercise") << "version 1\ntype intervals\ntitle Beta\ncategory Ear training\n";
    std::ofstream(dir / "a.exercise") << "version 1\ntype intervals\ntitle Alpha\ncategory Ear training\n";
    std::ofstream(dir / "broken.exercise") << "version 1\ntype nope\n";
    std::ofstream(dir / "notes.txt") << "not an exercise";

    std::vector<ExerciseEntry> entries = scanExercises(dir.string(), false);
    REQUIRE(entries.size() == 3);
    CHECK(entries[0].exercise.title == "Alpha"); // sorted by category, then title
    CHECK(entries[1].exercise.title == "Beta");
    CHECK(entries[2].exercise.title == "broken"); // falls back to the file name
    CHECK_FALSE(entries[2].error.empty());
    CHECK(entries[0].id == "user-a");
    CHECK(scanExercises((dir / "missing").string(), true).empty());
}

TEST_CASE("every exercise shipped with the game loads"){
    std::vector<ExerciseEntry> entries = scanExercises(OMT_RESOURCES_DIR "exercises", true);
    CHECK(entries.size() >= 4);
    for (const ExerciseEntry& entry : entries){
        CHECK_MESSAGE(entry.error.empty(), entry.error);
        CHECK(entry.id.rfind("builtin-", 0) == 0);
    }
}
