#pragma once

#include "core/journal.h"
#include "core/profile.h"

#include <string>

// The player's progress as the game runs: their journal and profile (core/journal, core/profile), each run recorded
// as it ends, and what it earned waiting to be shown (ui/rewards): its XP, a new level, the daily goal met,
// achievements.

void initPlayerProgress(const std::string& progressDir, int goalMinutes);
void setDailyGoal(int minutes);
void refreshPlayerProgress(); // worked out again: the day may have changed (the streak, today's minutes)
// A run ended (or a chapter passed): its date and time filled in, added to the journal, the profile worked out again,
// and what it earned queued to be shown
void recordActivity(Activity activity);
const PlayerProfile& playerProfile();
// The latest runs of one drill (or song...), by its id: oldest first, at most `count`
std::vector<Activity> recentRuns(const std::string& id, int count);

struct Reward {
    enum class Kind { Xp, Goal, Level, Achievement } kind = Kind::Xp;
    long long xp = 0;    // Xp: how much
    int level = 0;       // Level: the new one
    int achievement = 0; // Achievement: into achievements()
    int streak = 0;      // Goal: the streak it makes
};
bool takeReward(Reward& reward); // the oldest waiting, false for none
