#include "doctest/doctest.h"

#include "core/routine.h"

#include <filesystem>

TEST_CASE("dates become day numbers and back"){
    CHECK(daysFromDate(1970, 1, 1) == 0);
    CHECK(daysFromDate(1970, 1, 2) == 1);
    CHECK(daysFromDate(1969, 12, 31) == -1);
    CHECK(daysFromDate(2000, 3, 1) - daysFromDate(2000, 2, 28) == 2); // 2000 is a leap year (divisible by 400)
    CHECK(daysFromDate(1900, 3, 1) - daysFromDate(1900, 2, 28) == 1); // 1900 isn't (divisible by 100)
    CHECK(daysFromDate(2027, 1, 1) - daysFromDate(2026, 12, 31) == 1);

    // Every day for 400 years (a full leap cycle) comes back as the date it came from
    int year, month, day;
    int start = daysFromDate(1900, 1, 1), wrong = 0;
    for (int d = start; d < start + 146097; d++){
        dateFromDays(d, year, month, day);
        if (daysFromDate(year, month, day) != d) wrong++;
    }
    CHECK(wrong == 0);
    dateFromDays(daysFromDate(2024, 2, 29), year, month, day);
    CHECK(year == 2024);
    CHECK(month == 2);
    CHECK(day == 29);
}

TEST_CASE("the day streak"){
    RoutineProgress progress;
    int monday = daysFromDate(2026, 9, 21);
    CHECK(currentStreak(progress, monday) == 0);

    finishRoutine(progress, monday);
    CHECK(progress.streak == 1);
    CHECK(doneOnDay(progress, monday));

    finishRoutine(progress, monday);      // twice in a day: counted once
    CHECK(progress.streak == 1);
    CHECK(progress.completed == 2);

    finishRoutine(progress, monday + 1);
    finishRoutine(progress, monday + 2);
    CHECK(progress.streak == 3);
    CHECK(currentStreak(progress, monday + 3) == 3); // the next day, not done yet: still alive
    CHECK_FALSE(doneOnDay(progress, monday + 3));
    CHECK(currentStreak(progress, monday + 4) == 0); // a whole day missed: gone

    finishRoutine(progress, monday + 4);  // starts over
    CHECK(progress.streak == 1);
    CHECK(progress.bestStreak == 3);
    CHECK(progress.completed == 5);
}

TEST_CASE("routine progress survives a save and load"){
    RoutineProgress original{7, daysFromDate(2026, 9, 26), 4, 6};
    std::filesystem::path dir = std::filesystem::temp_directory_path() / "lahn_tests";
    std::filesystem::create_directories(dir);
    std::string path = (dir / "routine.txt").string(), error;
    REQUIRE(saveRoutineProgress(path, original, error));
    RoutineProgress loaded = loadRoutineProgress(path);
    CHECK(loaded.completed == 7);
    CHECK(loaded.lastDay == original.lastDay);
    CHECK(loaded.streak == 4);
    CHECK(loaded.bestStreak == 6);

    CHECK(loadRoutineProgress((dir / "never-played.txt").string()).lastDay == -1);
}
