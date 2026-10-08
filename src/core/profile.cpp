#include "core/profile.h"

#include "core/routine.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>

namespace {

const float LONGEST_RUN_S = 600.0f; // an untimed run counts this much practice at most (left open, it isn't practice)
const int RECENT_DAYS = 14;
const int COMEBACK_DAYS = 7;
const int KNOWN_NOTE_RIGHT = 5;     // a note played right this often is one the player knows

std::string dateOf(int day){
    int y, m, d;
    dateFromDays(day, y, m, d);
    char text[16];
    std::snprintf(text, sizeof text, "%04d-%02d-%02d", y, m, d);
    return text;
}

int dayOf(const std::string& date){
    if (date.size() != 10) return 0;
    return daysFromDate(std::atoi(date.substr(0, 4).c_str()), std::atoi(date.substr(5, 2).c_str()), std::atoi(date.substr(8, 2).c_str()));
}

const char* titleFor(int level){
    if (level < 3) return "Newcomer";
    if (level < 6) return "First notes";
    if (level < 10) return "Busker";
    if (level < 15) return "Garage band";
    if (level < 20) return "Open mic";
    if (level < 25) return "Pub stage";
    if (level < 30) return "Session player";
    if (level < 40) return "Touring musician";
    if (level < 50) return "Headliner";
    return "Legend";
}

} // namespace

long long xpToReach(int level){
    // Each level takes 50 XP more than the one before, from 100
    const long long n = std::max(1, level) - 1;
    return 100 * n + 25 * n * (n - 1);
}

LevelInfo levelFor(long long xp){
    LevelInfo info;
    int level = 1;
    while (xpToReach(level + 1) <= xp) level++;
    info.level = level;
    info.intoLevel = xp - xpToReach(level);
    info.forNext = xpToReach(level + 1) - xpToReach(level);
    info.title = titleFor(level);
    return info;
}

const std::vector<Achievement>& achievements(){
    static const std::vector<Achievement> ALL = {
        { "first-note", "First note", "Play your first note right", Metric::NotesRight, 1, Tier::Bronze },
        { "notes-100", "A hundred notes", "Play 100 notes right", Metric::NotesRight, 100, Tier::Bronze },
        { "notes-1000", "A thousand notes", "Play 1,000 notes right", Metric::NotesRight, 1000, Tier::Silver },
        { "notes-10000", "Ten thousand notes", "Play 10,000 notes right", Metric::NotesRight, 10000, Tier::Gold },
        { "known-12", "Every note", "Know 12 different notes (each played right 5 times)", Metric::NotesKnown, 12, Tier::Bronze },
        { "known-24", "Two octaves", "Know 24 different notes", Metric::NotesKnown, 24, Tier::Silver },
        { "clean-1", "Clean!", "Play a drill's pass clean", Metric::CleanPasses, 1, Tier::Bronze },
        { "clean-25", "Steady hands", "25 clean passes", Metric::CleanPasses, 25, Tier::Silver },
        { "clean-100", "Rock solid", "100 clean passes", Metric::CleanPasses, 100, Tier::Gold },
        { "challenge-1", "Challenge accepted", "Pass a drill at its challenge tempo", Metric::DrillsPassed, 1, Tier::Bronze },
        { "challenge-10", "Ten out of ten", "Pass 10 drills", Metric::DrillsPassed, 10, Tier::Silver },
        { "challenge-50", "Unstoppable", "Pass 50 drills", Metric::DrillsPassed, 50, Tier::Gold },
        { "perfect-1", "Flawless", "Every note right in a run", Metric::PerfectRuns, 1, Tier::Silver },
        { "combo-25", "In the zone", "25 notes in a row, right", Metric::BestCombo, 25, Tier::Bronze },
        { "combo-100", "Locked in", "100 notes in a row, right", Metric::BestCombo, 100, Tier::Silver },
        { "combo-500", "Machine", "500 notes in a row, right", Metric::BestCombo, 500, Tier::Gold },
        { "perfect-10", "Perfectionist", "10 flawless runs", Metric::PerfectRuns, 10, Tier::Gold },
        { "daily-1", "Today's challenge", "Pass a daily challenge", Metric::DailyChallenges, 1, Tier::Bronze },
        { "daily-7", "Seven days, seven challenges", "Pass 7 daily challenges", Metric::DailyChallenges, 7, Tier::Silver },
        { "daily-30", "Challenger", "Pass 30 daily challenges", Metric::DailyChallenges, 30, Tier::Gold },
        { "chapter-1", "Chapter one", "Pass a course's chapter", Metric::ChaptersPassed, 1, Tier::Bronze },
        { "chapter-20", "Bookworm", "Pass 20 chapters", Metric::ChaptersPassed, 20, Tier::Silver },
        { "level-1", "Level complete", "Complete a level of a course", Metric::LevelsDone, 1, Tier::Silver },
        { "course-1", "Graduate", "Complete a whole course", Metric::CoursesDone, 1, Tier::Gold },
        { "goal-1", "Goal!", "Meet your daily goal", Metric::GoalDays, 1, Tier::Bronze },
        { "streak-3", "On a roll", "Meet your daily goal 3 days in a row", Metric::BestStreak, 3, Tier::Bronze },
        { "streak-7", "Week warrior", "A 7-day streak", Metric::BestStreak, 7, Tier::Silver },
        { "streak-30", "Habit", "A 30-day streak", Metric::BestStreak, 30, Tier::Gold },
        { "streak-100", "Devotion", "A 100-day streak", Metric::BestStreak, 100, Tier::Gold },
        { "marathon", "Marathon", "An hour of practice in one day", Metric::BestDayMinutes, 60, Tier::Silver },
        { "hours-10", "Ten hours in", "10 hours of practice in all", Metric::MinutesTotal, 600, Tier::Silver },
        { "hours-100", "A hundred hours", "100 hours of practice in all", Metric::MinutesTotal, 6000, Tier::Gold },
        { "tempo-120", "Quick reader", "A clean pass at 120 bpm", Metric::BestTempo, 120, Tier::Silver },
        { "tempo-160", "Speed demon", "A clean pass at 160 bpm", Metric::BestTempo, 160, Tier::Gold },
        { "band-10", "Jam session", "10 clean passes with the band", Metric::BandPasses, 10, Tier::Bronze },
        { "song-1", "Encore", "Play a song to its end", Metric::SongsPlayed, 1, Tier::Bronze },
        { "song-fc", "Full combo", "A song with nothing missed", Metric::FullCombos, 1, Tier::Silver },
        { "song-s", "S rank", "An S (or better) on a song", Metric::SongsS, 1, Tier::Gold },
        { "walk-10", "Neck walker", "10 rounds in one game of Neck walk", Metric::BestGameRounds, 10, Tier::Silver },
        { "night-owl", "Night owl", "Practice after midnight", Metric::NightOwl, 1, Tier::Bronze },
        { "early-bird", "Early bird", "Practice before 8 in the morning", Metric::EarlyBird, 1, Tier::Bronze },
        { "comeback", "Welcome back", "Come back after a week away", Metric::Comebacks, 1, Tier::Bronze },
        { "two-instruments", "Multi-instrumentalist", "Play on two instruments", Metric::Instruments, 2, Tier::Silver },
        { "piano-1", "Keys too", "A clean run on the piano", Metric::PianoRuns, 1, Tier::Bronze },
    };
    return ALL;
}

bool PlayerProfile::isUnlocked(int achievement) const {
    return std::any_of(unlocked.begin(), unlocked.end(), [&](const Unlock& unlock){ return unlock.achievement == achievement; });
}

long long activityXp(const Activity& a, bool firstTimePassed){
    switch (a.kind){
        case ActivityKind::Drill:
            return a.right + (a.clean ? 15 : 0) + (a.challenge ? 25 : 0) + (a.challenge && firstTimePassed ? 50 : 0) + (a.perfect() ? 20 : 0);
        case ActivityKind::Notes:
            return 2LL * a.right + (a.clean ? 10 : 0) + (a.perfect() ? 10 : 0);
        case ActivityKind::Song: {
            long long xp = a.right + (a.clean ? 50 : 0);
            if (a.grade == "SS") xp += 100;
            else if (a.grade == "S") xp += 60;
            else if (a.grade == "A") xp += 30;
            else if (a.grade == "B") xp += 15;
            return xp;
        }
        case ActivityKind::Game: return 10LL * a.rounds;
        case ActivityKind::Chapter: return CHAPTER_XP + (a.unitDone ? UNIT_XP : 0) + (a.courseDone ? COURSE_XP : 0);
        default: return 0;
    }
}

PlayerProfile buildProfile(const std::vector<Activity>& journal, int goalMinutes, int today){
    PlayerProfile p;
    p.goalMinutes = std::max(1, goalMinutes);
    long long* m = p.metrics;
    std::set<std::string> instruments;
    int lastDay = -1, runDay = -100, run = 0, freezes = 0;
    for (const Activity& a : journal){
        const int day = dayOf(a.date);
        if (lastDay >= 0 && day - lastDay > COMEBACK_DAYS) m[(int)Metric::Comebacks]++;
        lastDay = std::max(lastDay, day);
        const bool first = a.kind == ActivityKind::Drill && a.challenge && !p.drillsPassed.count(a.id);
        const long long xp = activityXp(a, first);
        p.xp += xp;
        DayPractice& practice = p.days[a.date];
        practice.xp += xp;
        const float seconds = a.kind == ActivityKind::Chapter ? 0.0f : std::min(a.seconds, LONGEST_RUN_S);
        practice.seconds += seconds;
        p.totalSeconds += seconds;
        m[(int)Metric::MinutesTotal] = (long long)(p.totalSeconds / 60.0f);
        p.metrics[(int)Metric::BestDayMinutes] = std::max(m[(int)Metric::BestDayMinutes], (long long)(practice.seconds / 60.0f));
        // The daily goal, met the first time today: a little XP, and the streak goes on (or starts again)
        if (!practice.goalMet && practice.seconds >= p.goalMinutes * 60.0f){
            practice.goalMet = true;
            practice.xp += DAILY_GOAL_XP;
            p.xp += DAILY_GOAL_XP;
            m[(int)Metric::GoalDays]++;
            // The streak: the day after the last one met, or the days missed between covered by freezes held
            const int missed = day - runDay - 1;
            if (missed == 0) run++;
            else if (runDay >= 0 && missed > 0 && missed <= freezes){
                freezes -= missed;
                for (int frozen = runDay + 1; frozen < day; frozen++) p.frozenDays.insert(dateOf(frozen));
                run++;
            } else run = 1;
            runDay = day;
            m[(int)Metric::BestStreak] = std::max(m[(int)Metric::BestStreak], (long long)run);
            if (m[(int)Metric::GoalDays] % FREEZE_EVERY_GOAL_DAYS == 0){
                p.freezesEarned++;
                freezes = std::min(MOST_FREEZES, freezes + 1);
            }
        }
        // What it counts toward
        if (a.kind != ActivityKind::Chapter && a.kind != ActivityKind::Game) m[(int)Metric::NotesRight] += a.right;
        if (a.kind == ActivityKind::Drill){
            if (a.clean){
                m[(int)Metric::CleanPasses]++;
                m[(int)Metric::BestTempo] = std::max(m[(int)Metric::BestTempo], (long long)a.tempo);
                if (a.band) m[(int)Metric::BandPasses]++;
            }
            if (first){
                p.drillsPassed.insert(a.id);
                m[(int)Metric::DrillsPassed]++;
                if (a.id.rfind("daily-", 0) == 0) m[(int)Metric::DailyChallenges]++; // a day's challenge (Learn): its id has its date
            }
        }
        if ((a.kind == ActivityKind::Drill || a.kind == ActivityKind::Notes) && a.perfect() && a.total >= 4) m[(int)Metric::PerfectRuns]++;
        if (a.kind == ActivityKind::Song){
            m[(int)Metric::SongsPlayed]++;
            if (a.clean) m[(int)Metric::FullCombos]++;
            if (a.grade == "S" || a.grade == "SS") m[(int)Metric::SongsS]++;
        }
        if (a.kind == ActivityKind::Game) m[(int)Metric::BestGameRounds] = std::max(m[(int)Metric::BestGameRounds], (long long)a.rounds);
        m[(int)Metric::BestCombo] = std::max(m[(int)Metric::BestCombo], (long long)a.combo);
        // A chapter passed, and its drills played after (their ids its own and the step: "<chapter>-3"): its reviews
        if (a.kind == ActivityKind::Chapter && !p.chapters.count(a.id)){
            ChapterPractice& practice = p.chapters[a.id];
            practice.passedDay = practice.lastDay = day;
        }
        if (a.kind == ActivityKind::Drill || a.kind == ActivityKind::Notes){
            const size_t dash = a.id.rfind('-');
            auto chapter = dash == std::string::npos ? p.chapters.end() : p.chapters.find(a.id.substr(0, dash));
            if (chapter != p.chapters.end() && day > chapter->second.passedDay){
                chapter->second.reviewDays.insert(day);
                chapter->second.lastDay = std::max(chapter->second.lastDay, day);
            }
        }
        if (a.kind == ActivityKind::Chapter){
            m[(int)Metric::ChaptersPassed]++;
            if (a.unitDone) m[(int)Metric::LevelsDone]++;
            if (a.courseDone) m[(int)Metric::CoursesDone]++;
        }
        if (a.kind != ActivityKind::Chapter){
            if (a.minute < 4 * 60) m[(int)Metric::NightOwl]++;
            if (a.minute >= 5 * 60 && a.minute < 8 * 60) m[(int)Metric::EarlyBird]++;
            if (!a.instrument.empty() && a.instrument != "keys") instruments.insert(a.instrument);
            if (a.instrument == "piano" && a.clean) m[(int)Metric::PianoRuns]++;
        }
        m[(int)Metric::Instruments] = (long long)instruments.size();
        for (const NoteTally& note : a.notes){
            NoteTally& all = p.notes[note.pitch];
            const bool knewIt = all.right >= KNOWN_NOTE_RIGHT;
            all.pitch = note.pitch;
            all.right += note.right;
            all.asked += note.asked;
            if (!knewIt && all.right >= KNOWN_NOTE_RIGHT) m[(int)Metric::NotesKnown]++;
            if (today - day < RECENT_DAYS){
                for (NoteTally* recent : { &p.recentNotes[note.pitch], &p.recentByInstrument[a.instrument][note.pitch] }){
                    recent->pitch = note.pitch;
                    recent->right += note.right;
                    recent->asked += note.asked;
                }
            }
        }
        // Achievements reached with it, dated
        const std::vector<Achievement>& all = achievements();
        for (int i = 0; i < (int)all.size(); i++)
            if (m[(int)all[i].metric] >= all[i].goal && !p.isUnlocked(i)) p.unlocked.push_back({ i, a.date });
    }
    p.level = levelFor(p.xp);
    // The streak now: alive if the goal was met today, or yesterday (today isn't over)
    auto met = [&](int day){
        int y, mo, d;
        dateFromDays(day, y, mo, d);
        char date[16];
        std::snprintf(date, sizeof date, "%04d-%02d-%02d", y, mo, d);
        auto found = p.days.find(date);
        return found != p.days.end() && found->second.goalMet;
    };
    {
        int y, mo, d;
        dateFromDays(today, y, mo, d);
        char date[16];
        std::snprintf(date, sizeof date, "%04d-%02d-%02d", y, mo, d);
        auto found = p.days.find(date);
        p.secondsToday = found == p.days.end() ? 0.0f : found->second.seconds;
    }
    p.goalMetToday = met(today);
    // Today, or yesterday, or the days missed since covered by the freezes held (they're used): the streak goes on
    (void)met;
    if (runDay >= 0){
        const int missedSince = today - runDay - (p.goalMetToday ? 0 : 1); // the days gone by without the goal
        if (missedSince <= 0) p.streak = run;
        else if (missedSince <= freezes){
            p.streak = run;
            freezes -= missedSince;
            for (int frozen = runDay + 1; frozen <= runDay + missedSince; frozen++) p.frozenDays.insert(dateOf(frozen));
        } else {
            p.streak = 0;
            freezes = 0; // tried, and not enough
        }
    }
    p.freezes = freezes;
    p.bestStreak = (int)m[(int)Metric::BestStreak];
    return p;
}

ProfileChange profileChange(const PlayerProfile& before, const PlayerProfile& after){
    ProfileChange change;
    change.xp = after.xp - before.xp;
    if (after.level.level > before.level.level) change.newLevel = after.level.level;
    for (const Unlock& unlock : after.unlocked) if (!before.isUnlocked(unlock.achievement)) change.achievements.push_back(unlock.achievement);
    change.goalMet = after.goalMetToday && !before.goalMetToday;
    change.freezeEarned = after.freezesEarned > before.freezesEarned;
    change.streak = after.streak;
    return change;
}

std::vector<DueChapter> dueChapters(const PlayerProfile& profile, int today){
    static const int INTERVALS[] = { 3, 7, 14, 30, 60, 120 }; // days, by the reviews done
    std::vector<DueChapter> due;
    for (const auto& [id, practice] : profile.chapters){
        const int reviews = (int)practice.reviewDays.size();
        const int interval = INTERVALS[std::min(reviews, 5)];
        if (today - practice.lastDay >= interval) due.push_back({ id, today - practice.lastDay, reviews });
    }
    std::sort(due.begin(), due.end(), [](const DueChapter& a, const DueChapter& b){ return a.daysSince > b.daysSince; });
    return due;
}

std::vector<NoteTally> weakestNotes(const PlayerProfile& profile, int count, int minAsked, const std::string& instrument){
    std::vector<NoteTally> notes;
    static const std::map<int, NoteTally> NONE;
    auto found = profile.recentByInstrument.find(instrument);
    const std::map<int, NoteTally>& recent = instrument.empty() ? profile.recentNotes : found == profile.recentByInstrument.end() ? NONE : found->second;
    for (const auto& [pitch, tally] : recent) if (tally.asked >= minAsked && tally.right < tally.asked) notes.push_back(tally);
    std::sort(notes.begin(), notes.end(), [](const NoteTally& a, const NoteTally& b){
        return (double)a.right / a.asked < (double)b.right / b.asked;
    });
    if ((int)notes.size() > count) notes.resize((size_t)count);
    return notes;
}
