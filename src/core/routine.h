#pragma once

#include <string>

// A routine: a playlist of exercises done one after the other, a few minutes each (a daily warm-up).
// This is the logic only: the day streak and its progress file. Running the steps is learn/routineexercise's job.

struct RoutineStep {
    std::string exercise; // the exercise's file name, without ".exercise"
    float minutes;
};

// Days are counted from 1970-01-01, so "yesterday" is just today - 1, across months and years
int daysFromDate(int year, int month, int day);
void dateFromDays(int days, int& year, int& month, int& day);
int today(); // on the player's clock, in their time zone

struct RoutineProgress {
    int completed = 0;   // how many times the routine was finished
    int lastDay = -1;    // the day it was last finished; -1 = never
    int streak = 0;      // days in a row, ending on lastDay
    int bestStreak = 0;
};

// Finishing twice in one day counts once for the streak; missing a day starts it over
void finishRoutine(RoutineProgress& progress, int day);
int currentStreak(const RoutineProgress& progress, int day); // 0 once a whole day was missed
bool doneOnDay(const RoutineProgress& progress, int day);

RoutineProgress loadRoutineProgress(const std::string& path); // lenient, like all progress files
bool saveRoutineProgress(const std::string& path, const RoutineProgress& progress, std::string& error);
