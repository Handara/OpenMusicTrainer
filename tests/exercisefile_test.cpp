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
    CHECK(entries[2].error.rfind("broken.exercise:2: ", 0) == 0); // short: file name and line, not the full path
    CHECK(entries[0].id == "user-a");
    CHECK(scanExercises((dir / "missing").string(), true).empty());
}

TEST_CASE("every exercise shipped with the game loads"){
    std::vector<ExerciseEntry> entries = scanExercises(OMT_RESOURCES_DIR "exercises", true);
    checkRoutines(entries); // built-in routines may only use built-in exercises: all of them must be found
    CHECK(entries.size() >= 4);
    for (const ExerciseEntry& entry : entries){
        CHECK_MESSAGE(entry.error.empty(), entry.error);
        CHECK(entry.id.rfind("builtin-", 0) == 0);
    }
}

TEST_CASE("scale drill exercise files"){
    ExerciseFile file;
    std::string error;
    REQUIRE_MESSAGE(loadExerciseFile(writeExercise("drill.exercise",
        "version 1\ntype scale\ntitle A minor pentatonic\ncategory Technique\nkey A\nscale minor_pentatonic\n"
        "position 5\ndirection up\nnotes_per_beat 3\ntempo 70 150 5\npass 85\n"), file, error), error);
    CHECK(file.type == ExerciseType::Scale);
    CHECK(file.drill.rootPitchClass == 9);
    CHECK(file.drill.scale == "minor_pentatonic");
    CHECK(file.drill.position == 5);
    CHECK(file.drill.direction == DrillDirection::Up);
    CHECK(file.drill.notesPerBeat == 3);
    CHECK(file.drill.startTempo == 70);
    CHECK(file.drill.maxTempo == 150);
    CHECK(file.drill.passPercent == 85);

    // Everything optional: G major, the defaults
    REQUIRE(loadExerciseFile(writeExercise("drill2.exercise", "version 1\ntype scale\ntitle Defaults\n"), file, error));
    CHECK(file.drill.scale == "major");

    const std::string head = "version 1\ntype scale\ntitle T\n";
    struct Case { const char* name; std::string content; const char* expected; };
    const Case cases[] = {
        {"bad key",          head + "key H\n",               "key must be a note name"},
        {"unknown scale",    head + "scale bebop\n",         "unknown scale 'bebop'"},
        {"tempo backwards",  head + "tempo 120 80 4\n",      "start <= goal"},
        {"bad direction",    head + "direction sideways\n",  "direction must be up, down or up_down"},
        {"interval setting", head + "intervals 4 3\n",       "unknown setting 'intervals'"},
        {"doesn't fit",      head + "octaves 4\nposition 12\n", "doesn't fit around fret 12"},
    };
    for (const Case& c : cases){
        SUBCASE(c.name){
            CHECK_FALSE(loadExerciseFile(writeExercise("baddrill.exercise", c.content), file, error));
            CHECK_MESSAGE(error.find(c.expected) != std::string::npos, error);
        }
    }
}

TEST_CASE("routine exercise files"){
    ExerciseFile file;
    std::string error;
    REQUIRE_MESSAGE(loadExerciseFile(writeExercise("routine.exercise",
        "version 1\ntype routine\ntitle Warm-up\ncategory Routines\nstep e-minor-open 3\nstep intervals-up 2.5\n"), file, error), error);
    CHECK(file.type == ExerciseType::Routine);
    REQUIRE(file.routine.size() == 2);
    CHECK(file.routine[0].exercise == "e-minor-open");
    CHECK(file.routine[0].minutes == doctest::Approx(3.0f));
    CHECK(file.routine[1].minutes == doctest::Approx(2.5f));

    const std::string head = "version 1\ntype routine\ntitle T\n";
    struct Case { const char* name; std::string content; const char* expected; };
    const Case cases[] = {
        {"no steps",      head,                           "at least one 'step'"},
        {"no minutes",    head + "step warmup\n",         "expected: step <exercise file name> <minutes>"},
        {"zero minutes",  head + "step warmup 0\n",       "more than 0 and at most 60"},
        {"too long",      head + "step warmup 90\n",      "more than 0 and at most 60"},
        {"trailing text", head + "step warmup 3 fast\n",  "unexpected text after 'step'"},
        {"drill setting", head + "tempo 60 120 4\n",      "unknown setting 'tempo'"},
    };
    for (const Case& c : cases){
        SUBCASE(c.name){
            CHECK_FALSE(loadExerciseFile(writeExercise("badroutine.exercise", c.content), file, error));
            CHECK_MESSAGE(error.find(c.expected) != std::string::npos, error);
        }
    }
}

TEST_CASE("routine steps are found in the right folder"){
    fs::path builtInDir = testDir() / "routines-builtin", userDir = testDir() / "routines-user";
    fs::remove_all(builtInDir);
    fs::remove_all(userDir);
    fs::create_directories(builtInDir);
    fs::create_directories(userDir);
    const std::string intervals = "version 1\ntype intervals\n";
    std::ofstream(builtInDir / "ears.exercise") << intervals << "title Built-in ears\n";
    std::ofstream(builtInDir / "daily.exercise") << "version 1\ntype routine\ntitle Daily\nstep ears 5\n";
    std::ofstream(builtInDir / "needs-yours.exercise") << "version 1\ntype routine\ntitle N\nstep mine 5\n";
    std::ofstream(userDir / "ears.exercise") << intervals << "title My ears\n";
    std::ofstream(userDir / "mine.exercise") << intervals << "title Mine\n";
    std::ofstream(userDir / "my-daily.exercise") << "version 1\ntype routine\ntitle My daily\nstep ears 5\nstep daily 5\n";
    std::ofstream(userDir / "typo.exercise") << "version 1\ntype routine\ntitle Typo\nstep eras 5\n";
    std::ofstream(userDir / "broken.exercise") << "version 1\ntype nope\n";
    std::ofstream(userDir / "uses-broken.exercise") << "version 1\ntype routine\ntitle U\nstep broken 5\n";
    std::ofstream(userDir / "only-builtin.exercise") << "version 1\ntype routine\ntitle O\nstep mine 1\nstep ears 1\n";

    std::vector<ExerciseEntry> entries = scanExercises(builtInDir.string(), true);
    std::vector<ExerciseEntry> user = scanExercises(userDir.string(), false);
    entries.insert(entries.end(), user.begin(), user.end());
    checkRoutines(entries);
    auto entry = [&](const std::string& name, bool builtIn) -> const ExerciseEntry& {
        for (const ExerciseEntry& e : entries) if (e.name == name && e.builtIn == builtIn) return e;
        FAIL("no entry " << name);
        return entries[0];
    };

    // A built-in routine uses the built-in exercise, even when the player has one with the same name
    const ExerciseEntry& daily = entry("daily", true);
    CHECK(daily.error.empty());
    CHECK(findRoutineStep(entries, daily, "ears")->exercise.title == "Built-in ears");
    CHECK(entry("needs-yours", true).error.find("there's no mine.exercise") != std::string::npos);

    // The player's routines look in their own exercises first, then the built-in ones
    const ExerciseEntry& onlyBuiltIn = entry("only-builtin", false);
    CHECK(onlyBuiltIn.error.empty());
    CHECK(findRoutineStep(entries, onlyBuiltIn, "ears")->exercise.title == "My ears");
    CHECK(findRoutineStep(entries, onlyBuiltIn, "mine")->exercise.title == "Mine");

    CHECK(entry("my-daily", false).error == "my-daily.exercise: step 'daily' is a routine: routines can't contain routines");
    CHECK(entry("typo", false).error.find("there's no eras.exercise") != std::string::npos);
    CHECK(entry("uses-broken", false).error.find("step 'broken' has an error of its own") != std::string::npos);
}
