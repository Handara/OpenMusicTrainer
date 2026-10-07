#include "app/playerprogress.h"

#include "core/routine.h"
#include "raylib.h"

#include <ctime>
#include <deque>
#include <filesystem>

namespace {
struct PlayerProgressState {
    bool ready = false;
    std::string journalPath;
    std::vector<Activity> journal;
    PlayerProfile profile;
    int goalMinutes = 10;
    std::deque<Reward> rewards;
};
}
static PlayerProgressState progress;

static void rebuild(){
    progress.profile = buildProfile(progress.journal, progress.goalMinutes, today());
}

void initPlayerProgress(const std::string& progressDir, int goalMinutes){
    progress.journalPath = (std::filesystem::path(progressDir) / "journal.txt").string();
    progress.journal = loadJournal(progress.journalPath);
    progress.goalMinutes = goalMinutes;
    progress.ready = true;
    rebuild();
}

void setDailyGoal(int minutes){
    progress.goalMinutes = minutes;
    rebuild();
}

void refreshPlayerProgress(){
    if (progress.ready) rebuild();
}

void recordActivity(Activity activity){
    if (!progress.ready) return;
    // Now, on the player's clock
    int year, month, day;
    dateFromDays(today(), year, month, day);
    activity.date = TextFormat("%04d-%02d-%02d", year, month, day);
    const std::time_t now = std::time(nullptr);
    std::tm local{};
#if defined(_WIN32)
    localtime_s(&local, &now);
#else
    localtime_r(&now, &local);
#endif
    activity.minute = local.tm_hour * 60 + local.tm_min;

    rebuild(); // as it is now (the day may have changed since the last)
    const PlayerProfile before = progress.profile;
    progress.journal.push_back(activity);
    std::string error;
    if (!appendToJournal(progress.journalPath, activity, error)) TraceLog(LOG_WARNING, "Progress: %s", error.c_str());
    rebuild();
    const ProfileChange change = profileChange(before, progress.profile);
    if (change.xp > 0) progress.rewards.push_back({ Reward::Kind::Xp, change.xp, 0, 0, 0 });
    if (change.goalMet) progress.rewards.push_back({ Reward::Kind::Goal, 0, 0, 0, change.streak });
    if (change.newLevel > 0) progress.rewards.push_back({ Reward::Kind::Level, 0, change.newLevel, 0, 0 });
    for (int achievement : change.achievements) progress.rewards.push_back({ Reward::Kind::Achievement, 0, 0, achievement, 0 });
}

const PlayerProfile& playerProfile(){
    return progress.profile;
}

bool takeReward(Reward& reward){
    if (progress.rewards.empty()) return false;
    reward = progress.rewards.front();
    progress.rewards.pop_front();
    return true;
}
