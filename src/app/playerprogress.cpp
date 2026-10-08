#include "app/playerprogress.h"

#include "core/routine.h"
#include "raylib.h"

#include <algorithm>
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
    auto reward = [](Reward::Kind kind){ Reward made; made.kind = kind; return made; };
    if (change.xp > 0){
        Reward xp = reward(Reward::Kind::Xp);
        xp.xp = change.xp;
        progress.rewards.push_back(xp);
    }
    if (activity.kind == ActivityKind::Chapter){ // its stars are its right and total
        Reward chapter = reward(Reward::Kind::Chapter);
        chapter.title = activity.title;
        chapter.stars = activity.right;
        chapter.starsPossible = activity.total;
        chapter.unitDone = activity.unitDone;
        chapter.courseDone = activity.courseDone;
        progress.rewards.push_back(chapter);
    }
    if (change.goalMet){
        Reward goal = reward(Reward::Kind::Goal);
        goal.streak = change.streak;
        progress.rewards.push_back(goal);
    }
    if (change.freezeEarned){
        Reward freeze = reward(Reward::Kind::Freeze);
        freeze.streak = progress.profile.freezes;
        progress.rewards.push_back(freeze);
    }
    if (change.newLevel > 0){
        Reward level = reward(Reward::Kind::Level);
        level.level = change.newLevel;
        progress.rewards.push_back(level);
    }
    for (int achievement : change.achievements){
        Reward earned = reward(Reward::Kind::Achievement);
        earned.achievement = achievement;
        progress.rewards.push_back(earned);
    }
}

std::vector<Activity> recentRuns(const std::string& id, int count){
    std::vector<Activity> runs;
    for (auto it = progress.journal.rbegin(); it != progress.journal.rend() && (int)runs.size() < count; ++it)
        if (it->id == id) runs.push_back(*it);
    std::reverse(runs.begin(), runs.end());
    return runs;
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
