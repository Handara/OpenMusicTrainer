#pragma once

#include "core/exercisefile.h"

#include <string>
#include <vector>

// What the main menu's "today" panel says, from the player's progress files: a reason to come back.
struct TodaySummary {
    // The routine to keep up: the one with the longest streak still alive, else the first routine there is
    bool hasRoutine = false;
    std::string routineTitle;
    float routineMinutes = 0.0f;
    bool routineDoneToday = false;
    int streakDays = 0;          // that routine's current day streak

    // The next goal: the drill practiced most recently, at the tempo it will ask for next
    bool hasDrill = false;
    std::string drillTitle;
    int drillTempo = 0;
};

// `exercises`: every exercise (built-in and the player's), `progressDir`: where their progress files are,
// `day`: today (core/routine's day numbers)
TodaySummary summarizeToday(const std::vector<ExerciseEntry>& exercises, const std::string& progressDir, int day);
