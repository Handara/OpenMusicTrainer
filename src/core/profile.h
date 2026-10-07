#pragma once

#include "core/journal.h"

#include <map>
#include <set>
#include <string>
#include <vector>

// The player's progress, worked out from their journal (core/journal): XP and a level for everything played, the
// minutes practiced each day against a daily goal and the streak of days it was met, how well they know each note,
// and achievements, each unlocked the day it was earned. Replayed from the start each time, so it's always what the
// journal says. Pure.

// --- XP and levels ------------------------------------------------------------------------------------------

// What a run earns: a point a note right (two for an untimed run's), more for a clean pass, a drill's challenge (the
// first time most), every note right, a song's grade; a game's rounds; a chapter, a level and a course passed
const int CHAPTER_XP = 100, UNIT_XP = 250, COURSE_XP = 1000, DAILY_GOAL_XP = 20;

struct LevelInfo {
    int level = 1;
    long long intoLevel = 0; // XP since this level began
    long long forNext = 100; // XP this level takes
    const char* title = "";  // what a player of this level is called: "Busker", "Session player"
};
LevelInfo levelFor(long long xp);
long long xpToReach(int level); // from nothing; level 1 at 0

// --- Achievements -------------------------------------------------------------------------------------------

// What an achievement counts
enum class Metric {
    NotesRight, CleanPasses, DrillsPassed, PerfectRuns, ChaptersPassed, LevelsDone, CoursesDone, BestStreak, GoalDays,
    MinutesTotal, BestDayMinutes, BestTempo, BandPasses, SongsPlayed, FullCombos, SongsS, BestGameRounds, NightOwl,
    EarlyBird, Comebacks, Instruments, PianoRuns, NotesKnown, Count
};
enum class Tier { Bronze, Silver, Gold };

struct Achievement {
    const char* id;
    const char* name;
    const char* description;
    Metric metric;
    long long goal;
    Tier tier;
};
const std::vector<Achievement>& achievements();

// --- The profile --------------------------------------------------------------------------------------------

struct DayPractice {
    float seconds = 0.0f;
    long long xp = 0;
    bool goalMet = false;
};

struct Unlock {
    int achievement = 0; // into achievements()
    std::string date;
};

struct PlayerProfile {
    long long xp = 0;
    LevelInfo level;
    int goalMinutes = 10;
    std::map<std::string, DayPractice> days; // by date
    int streak = 0;            // days in a row the goal was met, up to today (or yesterday: today isn't lost yet)
    int bestStreak = 0;
    bool goalMetToday = false;
    float secondsToday = 0.0f;
    float totalSeconds = 0.0f;
    long long metrics[(int)Metric::Count] = {};
    std::set<std::string> drillsPassed; // passed at their challenge
    std::map<int, NoteTally> notes;      // by pitch: every note asked, how often right
    std::map<int, NoteTally> recentNotes; //   the same, the last 14 days only
    std::vector<Unlock> unlocked;        // in the order earned
    bool isUnlocked(int achievement) const;
};

// `today`: the player's day (core/routine day numbers), for the streak and what's recent
PlayerProfile buildProfile(const std::vector<Activity>& journal, int goalMinutes, int today);

// The XP one activity earns, given what was played before it (a drill's challenge passed the first time earns more)
long long activityXp(const Activity& activity, bool firstTimePassed);

// What one more run changed, for the rewards shown: XP gained, a new level, achievements, the daily goal just met
struct ProfileChange {
    long long xp = 0;
    int newLevel = 0;      // 0 for none
    std::vector<int> achievements;
    bool goalMet = false;  // just now
    int streak = 0;
};
ProfileChange profileChange(const PlayerProfile& before, const PlayerProfile& after);

// The notes asked most often wrong lately (at least `minAsked` times asked), weakest first
std::vector<NoteTally> weakestNotes(const PlayerProfile& profile, int count, int minAsked = 4);
