#include "doctest/doctest.h"

#include "core/drill.h"
#include "core/routine.h"
#include "core/today.h"

#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

static ExerciseEntry routine(const std::string& id, const std::string& title, std::vector<RoutineStep> steps){
    ExerciseEntry entry;
    entry.id = id;
    entry.exercise.type = ExerciseType::Routine;
    entry.exercise.title = title;
    entry.exercise.routine = steps;
    return entry;
}

static ExerciseEntry drill(const std::string& id, const std::string& title){
    ExerciseEntry entry;
    entry.id = id;
    entry.exercise.type = ExerciseType::Scale;
    entry.exercise.title = title;
    return entry;
}

TEST_CASE("today's summary: the routine to keep up and the next drill"){
    fs::path dir = fs::temp_directory_path() / "lahn_tests" / "today";
    fs::remove_all(dir);
    fs::create_directories(dir);
    int day = daysFromDate(2026, 9, 27);
    std::vector<ExerciseEntry> exercises = {
        routine("builtin-warm-up", "Daily warm-up", {{"a", 3}, {"b", 4.5f}}),
        routine("builtin-ears", "Ear training mix", {{"c", 10}}),
        drill("builtin-e-minor", "E minor, open position"),
        drill("builtin-g-major", "G major, 2nd position"),
    };

    // Nothing practiced yet: the first routine, no streak, no drill
    TodaySummary fresh = summarizeToday(exercises, dir.string(), day);
    CHECK(fresh.hasRoutine);
    CHECK(fresh.routineTitle == "Daily warm-up");
    CHECK(fresh.routineMinutes == doctest::Approx(7.5f));
    CHECK_FALSE(fresh.routineDoneToday);
    CHECK(fresh.streakDays == 0);
    CHECK_FALSE(fresh.hasDrill);

    // The ear training routine has a 3-day streak going, done today: it's the one to keep up
    std::string error;
    REQUIRE(saveRoutineProgress((dir / "builtin-ears.txt").string(), {3, day, 3, 3}, error));
    REQUIRE(saveRoutineProgress((dir / "builtin-warm-up.txt").string(), {1, day - 5, 1, 1}, error)); // long gone
    // Two drills practiced: the newer file is the drill played last
    REQUIRE(saveDrillProgress((dir / "builtin-g-major.txt").string(), {72, 68, 4, 2}, error));
    fs::last_write_time(dir / "builtin-g-major.txt", fs::file_time_type::clock::now() - std::chrono::hours(2));
    REQUIRE(saveDrillProgress((dir / "builtin-e-minor.txt").string(), {64, 60, 3, 1}, error));

    TodaySummary summary = summarizeToday(exercises, dir.string(), day);
    CHECK(summary.routineTitle == "Ear training mix");
    CHECK(summary.streakDays == 3);
    CHECK(summary.routineDoneToday);
    CHECK(summary.hasDrill);
    CHECK(summary.drillTitle == "E minor, open position");
    CHECK(summary.drillTempo == 64);

    // A broken exercise is skipped
    exercises[1].error = "broken";
    CHECK(summarizeToday(exercises, dir.string(), day).routineTitle == "Daily warm-up");
}
