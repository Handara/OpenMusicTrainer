#include "doctest/doctest.h"

#include "core/journal.h"
#include "core/profile.h"
#include "core/routine.h"

#include <algorithm>
#include <cstdio>
#include <string>

static std::string dateOf(int day){
    int y, m, d;
    dateFromDays(day, y, m, d);
    char text[16];
    std::snprintf(text, sizeof text, "%04d-%02d-%02d", y, m, d);
    return text;
}

static Activity drillPass(int day, int right, int total, bool clean, bool challenge, float seconds = 30.0f){
    Activity a;
    a.kind = ActivityKind::Drill;
    a.date = dateOf(day);
    a.minute = 18 * 60;
    a.id = "reading-e";
    a.title = "E alone";
    a.instrument = "guitar";
    a.seconds = seconds;
    a.right = right;
    a.total = total;
    a.tempo = 100;
    a.clean = clean;
    a.challenge = challenge;
    return a;
}

const int DAY = 20000; // a day, in core/routine's numbers

TEST_CASE("journal: an activity survives being written and read back, its title and notes with it"){
    Activity a = drillPass(DAY, 38, 40, true, true);
    a.title = "E and F, shown where";
    a.band = true;
    a.notes = { { 64, 20, 21 }, { 65, 18, 19 } };
    Activity read;
    REQUIRE(readActivity(writeActivity(a), read));
    CHECK(read.date == a.date);
    CHECK(read.minute == 18 * 60);
    CHECK(read.kind == ActivityKind::Drill);
    CHECK(read.id == "reading-e");
    CHECK(read.title == "E and F, shown where");
    CHECK(read.right == 38);
    CHECK(read.total == 40);
    CHECK(read.clean);
    CHECK(read.challenge);
    CHECK(read.band);
    REQUIRE(read.notes.size() == 2);
    CHECK(read.notes[1].pitch == 65);
    CHECK(read.notes[1].right == 18);
    CHECK(read.notes[1].asked == 19);
    CHECK_FALSE(readActivity("version 1", read));
    CHECK_FALSE(readActivity("2026-10-07 18:00 teleport id=x", read)); // a kind from a newer lahn: skipped
}

TEST_CASE("levels: each takes 50 XP more than the last, from 100"){
    CHECK(xpToReach(1) == 0);
    CHECK(xpToReach(2) == 100);
    CHECK(xpToReach(3) == 250);
    CHECK(xpToReach(4) == 450);
    CHECK(levelFor(99).level == 1);
    CHECK(levelFor(100).level == 2);
    CHECK(levelFor(100).intoLevel == 0);
    CHECK(levelFor(300).level == 3);
    CHECK(levelFor(300).intoLevel == 50);
    CHECK(levelFor(300).forNext == 200);
    CHECK(std::string(levelFor(0).title) == "Newcomer");
}

TEST_CASE("XP: notes right, a clean pass, the challenge passed the first time most"){
    const Activity pass = drillPass(DAY, 40, 40, true, true);
    CHECK(activityXp(pass, true) == 40 + 15 + 25 + 50 + 20);
    CHECK(activityXp(pass, false) == 40 + 15 + 25 + 20);
    const PlayerProfile profile = buildProfile({ pass, pass }, 10, DAY);
    CHECK(profile.xp == activityXp(pass, true) + activityXp(pass, false));
    CHECK(profile.drillsPassed.size() == 1);
}

TEST_CASE("the daily goal and the streak: days in a row it's met, alive until a whole day is missed"){
    std::vector<Activity> journal;
    for (int day = DAY; day < DAY + 3; day++){
        journal.push_back(drillPass(day, 30, 40, false, false, 400.0f));
        journal.push_back(drillPass(day, 30, 40, false, false, 300.0f)); // 11 minutes and a bit, that day
    }
    PlayerProfile today = buildProfile(journal, 10, DAY + 2);
    CHECK(today.goalMetToday);
    CHECK(today.streak == 3);
    CHECK(today.bestStreak == 3);
    CHECK(buildProfile(journal, 10, DAY + 3).streak == 3); // the next day: not lost yet
    CHECK(buildProfile(journal, 10, DAY + 4).streak == 0); // a day missed
    CHECK(buildProfile(journal, 20, DAY + 2).streak == 0); // a goal of 20 minutes was never met
    CHECK(today.xp > 0);
    CHECK(today.days.at(dateOf(DAY)).goalMet);
}

TEST_CASE("streak freezes: one earned every 7 days the goal is met, covering a day missed"){
    std::vector<Activity> journal;
    for (int day = DAY; day < DAY + 7; day++) journal.push_back(drillPass(day, 30, 40, false, false, 700.0f));
    PlayerProfile week = buildProfile(journal, 10, DAY + 6);
    CHECK(week.streak == 7);
    CHECK(week.freezes == 1);
    CHECK(week.freezesEarned == 1);
    // A day missed (DAY + 7), then the goal met again: the streak goes on, the freeze used
    journal.push_back(drillPass(DAY + 8, 30, 40, false, false, 700.0f));
    PlayerProfile after = buildProfile(journal, 10, DAY + 8);
    CHECK(after.streak == 8);
    CHECK(after.freezes == 0);
    CHECK(after.frozenDays.count(dateOf(DAY + 7)) == 1);
    // Yesterday missed and today not played yet: the freeze held covers it, the streak's alive
    PlayerProfile pending = buildProfile(std::vector<Activity>(journal.begin(), journal.begin() + 7), 10, DAY + 8);
    CHECK(pending.streak == 7);
    CHECK(pending.freezes == 0);
    // Two days missed with one freeze: gone
    CHECK(buildProfile(std::vector<Activity>(journal.begin(), journal.begin() + 7), 10, DAY + 9).streak == 0);
}

TEST_CASE("achievements unlock the day they're earned, in order"){
    std::vector<Activity> journal = { drillPass(DAY, 3, 40, false, false) };
    journal.push_back(drillPass(DAY + 1, 40, 40, true, true));
    journal.push_back(drillPass(DAY + 10, 20, 40, false, false)); // back after a week and more
    const PlayerProfile profile = buildProfile(journal, 10, DAY + 10);
    auto unlockedOn = [&](const std::string& id) -> std::string {
        for (const Unlock& unlock : profile.unlocked) if (achievements()[(size_t)unlock.achievement].id == id) return unlock.date;
        return "";
    };
    CHECK(unlockedOn("first-note") == dateOf(DAY));
    CHECK(unlockedOn("clean-1") == dateOf(DAY + 1));
    CHECK(unlockedOn("challenge-1") == dateOf(DAY + 1));
    CHECK(unlockedOn("perfect-1") == dateOf(DAY + 1));
    CHECK(unlockedOn("comeback") == dateOf(DAY + 10));
    CHECK(unlockedOn("notes-1000").empty());
    // What one more run changed: its XP, what it unlocked
    const PlayerProfile before = buildProfile({ journal[0] }, 10, DAY + 1);
    const PlayerProfile after = buildProfile({ journal[0], journal[1] }, 10, DAY + 1);
    const ProfileChange change = profileChange(before, after);
    CHECK(change.xp == after.xp - before.xp);
    CHECK(change.achievements.size() >= 3);
}

TEST_CASE("the weakest notes: the ones most often wrong lately"){
    Activity a = drillPass(DAY, 0, 0, false, false);
    a.notes = { { 64, 9, 10 }, { 65, 2, 10 }, { 67, 6, 10 }, { 69, 1, 2 } }; // A asked too few times to tell
    const std::vector<NoteTally> weak = weakestNotes(buildProfile({ a }, 10, DAY), 2);
    REQUIRE(weak.size() == 2);
    CHECK(weak[0].pitch == 65);
    CHECK(weak[1].pitch == 67);
    CHECK(weakestNotes(buildProfile({ a }, 10, DAY + 30), 2).empty()); // a month on: not recent any more
    CHECK(weakestNotes(buildProfile({ a }, 10, DAY), 2, 4, "guitar").size() == 2); // played on the guitar
    CHECK(weakestNotes(buildProfile({ a }, 10, DAY), 2, 4, "bass").empty());
}

TEST_CASE("spaced repetition: a chapter passed comes back after 3 days, then after longer each time it's reviewed"){
    Activity passed;
    passed.kind = ActivityKind::Chapter;
    passed.date = dateOf(DAY);
    passed.id = "course-reading-the-high-e-string-e";
    std::vector<Activity> journal = { passed };
    CHECK(dueChapters(buildProfile(journal, 10, DAY + 2), DAY + 2).empty());
    std::vector<DueChapter> due = dueChapters(buildProfile(journal, 10, DAY + 3), DAY + 3);
    REQUIRE(due.size() == 1);
    CHECK(due[0].id == passed.id);
    CHECK(due[0].daysSince == 3);
    // Reviewed (one of its drills played) on the fourth day: next due a week after that
    Activity review = drillPass(DAY + 4, 38, 40, true, true);
    review.id = passed.id + "-3";
    journal.push_back(review);
    CHECK(dueChapters(buildProfile(journal, 10, DAY + 10), DAY + 10).empty());
    CHECK(dueChapters(buildProfile(journal, 10, DAY + 11), DAY + 11).size() == 1);
}
