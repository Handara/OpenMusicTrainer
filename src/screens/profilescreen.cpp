#include "screens/profilescreen.h"

#include "app/playerprogress.h"
#include "core/music.h"
#include "core/routine.h"
#include "imgui.h"
#include "raylib.h"
#include "ui/menulist.h"
#include "ui/rewards.h"
#include "ui/sharecard.h"
#include "ui/theme.h"
#include "ui/ui.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <string>

const int GOAL_CHOICES[] = { 5, 10, 15, 20, 30, 45, 60 }; // minutes a day
const int CALENDAR_WEEKS = 16;
const int REVIEW_NOTES = 6;
const int MEDALS_A_ROW = 10;

void openProfileScreen(){
    refreshPlayerProgress(); // the day may have changed
}

static std::string dateText(int day){
    int y, m, d;
    dateFromDays(day, y, m, d);
    char text[16];
    std::snprintf(text, sizeof text, "%04d-%02d-%02d", y, m, d);
    return text;
}

// "3 h 20 min", "45 min"
static std::string durationText(float seconds){
    const int minutes = (int)(seconds / 60.0f);
    if (minutes < 60) return std::to_string(minutes) + " min";
    return std::to_string(minutes / 60) + " h " + std::to_string(minutes % 60) + " min";
}

// A number with its thousands apart: 12,340
static std::string thousands(long long value){
    std::string digits = std::to_string(value), out;
    for (size_t i = 0; i < digits.size(); i++){
        if (i > 0 && (digits.size() - i) % 3 == 0) out += ",";
        out += digits[i];
    }
    return out;
}

// A tile: a label, a big number, a line under it
static void tile(ImDrawList* draw, ImVec2 a, ImVec2 b, const char* label, const std::string& value, const std::string& under, UiColor color, float s){
    const UiFonts& fonts = uiFonts();
    draw->AddRectFilled(a, b, uiColor(UiColor::Card), 10 * s);
    draw->AddText(fonts.mono, 11 * s, ImVec2(a.x + 14 * s, a.y + 12 * s), uiColor(UiColor::Dim), label);
    draw->AddText(fonts.heavy, 26 * s, ImVec2(a.x + 14 * s, a.y + 28 * s), uiColor(color), value.c_str());
    draw->AddText(fonts.text, 12 * s, ImVec2(a.x + 14 * s, a.y + 60 * s), uiColor(UiColor::Dim), under.c_str());
}

bool profileScreen(Settings& settings){
    bool goalChanged = false;
    beginMenu("Profile");
    menuTitle(settings.playerName.empty() ? "Profile" : settings.playerName.c_str());
    const PlayerProfile& p = playerProfile();
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const UiFonts& fonts = uiFonts();
    const float s = menuScale(), width = ImGui::GetWindowWidth(), height = ImGui::GetWindowHeight();
    const float left = width * 0.07f, right = width * 0.93f, middle = width * 0.5f, top = height * 0.17f;

    // --- The level: its badge, its title, the XP to the next ---
    const ImVec2 badge(left + 40 * s, top + 40 * s);
    draw->AddCircleFilled(badge, 40 * s, uiColor(UiColor::Accent), 48);
    const std::string level = std::to_string(p.level.level);
    ImVec2 size = fonts.heavy->CalcTextSizeA(34 * s, FLT_MAX, 0.0f, level.c_str());
    draw->AddText(fonts.heavy, 34 * s, ImVec2(badge.x - size.x / 2, badge.y - size.y / 2), uiColor(UiColor::Background), level.c_str());
    const float textX = left + 96 * s, barRight = middle - 30 * s;
    draw->AddText(fonts.mono, 12 * s, ImVec2(textX, top + 2 * s), uiColor(UiColor::Dim), TextFormat("LEVEL %d", p.level.level));
    // The ranked number: what their best runs of songs are worth (core/difficulty), each part's best, added up
    const char* pp = TextFormat("%s pp", thousands((long long)p.totalPp).c_str());
    const float ppWidth = fonts.heavy->CalcTextSizeA(22 * s, FLT_MAX, 0.0f, pp).x;
    draw->AddText(fonts.heavy, 22 * s, ImVec2(barRight - ppWidth, top + 18 * s), uiColor(p.totalPp > 0.0f ? UiColor::Accent : UiColor::Dim), pp);
    draw->AddText(fonts.mono, 12 * s, ImVec2(barRight - ppWidth, top + 2 * s), uiColor(UiColor::Dim), "RANKED");
    draw->AddText(fonts.heavy, 28 * s, ImVec2(textX, top + 16 * s), uiColor(UiColor::Ink), p.level.title);
    const float fill = (float)p.level.intoLevel / (float)std::max(1LL, p.level.forNext);
    draw->AddRectFilled(ImVec2(textX, top + 58 * s), ImVec2(barRight, top + 66 * s), uiColor(UiColor::StaffLine), 4 * s);
    draw->AddRectFilled(ImVec2(textX, top + 58 * s), ImVec2(textX + (barRight - textX) * fill, top + 66 * s), uiColor(UiColor::Accent), 4 * s);
    draw->AddText(fonts.mono, 12 * s, ImVec2(textX, top + 72 * s), uiColor(UiColor::Dim),
                  TextFormat("%s / %s XP to level %d   ·   %s XP in all", thousands(p.level.intoLevel).c_str(), thousands(p.level.forNext).c_str(),
                             p.level.level + 1, thousands(p.xp).c_str()));

    // --- What they've done: four tiles ---
    const float tilesTop = top + 104 * s, tileGap = 10 * s, tileWidth = (middle - 30 * s - left - 3 * tileGap) / 4, tileHeight = 82 * s;
    const long long right_ = p.metrics[(int)Metric::NotesRight];
    long long asked = 0, rightAsked = 0;
    for (const auto& [pitch, tally] : p.notes){
        asked += tally.asked;
        rightAsked += tally.right;
    }
    const int accuracy = asked > 0 ? (int)(rightAsked * 100 / asked) : 0;
    struct Tile { const char* label; std::string value, under; UiColor color; };
    const Tile tiles[4] = {
        { "PRACTICED", durationText(p.totalSeconds), std::to_string(p.days.size()) + (p.days.size() == 1 ? " day" : " days"), UiColor::Ink },
        { "NOTES RIGHT", thousands(right_), TextFormat("%d notes known", (int)p.metrics[(int)Metric::NotesKnown]), UiColor::Ink },
        { "ACCURACY", asked > 0 ? std::to_string(accuracy) + "%" : "-", "of every note read", accuracy >= 90 ? UiColor::Good : UiColor::Ink },
        { "STREAK", std::to_string(p.streak), p.freezes > 0 ? TextFormat("best %d  ·  %d %s", p.bestStreak, p.freezes, p.freezes == 1 ? "freeze" : "freezes")
                                                            : TextFormat("best %d %s", p.bestStreak, p.bestStreak == 1 ? "day" : "days"),
          p.streak > 0 ? UiColor::Accent : UiColor::Ink },
    };
    for (int i = 0; i < 4; i++){
        const float x = left + i * (tileWidth + tileGap);
        tile(draw, ImVec2(x, tilesTop), ImVec2(x + tileWidth, tilesTop + tileHeight), tiles[i].label, tiles[i].value, tiles[i].under, tiles[i].color, s);
    }

    // --- The daily goal, and the calendar of the last weeks ---
    const float goalTop = tilesTop + tileHeight + 22 * s;
    draw->AddText(fonts.mono, 12 * s, ImVec2(left, goalTop), uiColor(UiColor::Dim), "DAILY GOAL");
    const std::string goalText = TextFormat("%d min a day", settings.dailyGoalMinutes);
    draw->AddText(fonts.bold, 18 * s, ImVec2(left + 96 * s, goalTop - 4 * s), uiColor(UiColor::Ink), goalText.c_str());
    const float todayFill = std::min(1.0f, p.secondsToday / (settings.dailyGoalMinutes * 60.0f));
    const std::string todayText = p.goalMetToday ? "met today" : TextFormat("%d of %d min today", (int)(p.secondsToday / 60.0f), settings.dailyGoalMinutes);
    draw->AddText(fonts.text, 14 * s, ImVec2(left + 230 * s, goalTop - 1 * s), uiColor(p.goalMetToday ? UiColor::Good : UiColor::Dim), todayText.c_str());
    draw->AddText(fonts.mono, 11 * s, ImVec2(left, goalTop + 20 * s), uiColor(UiColor::Dim), "Left / Right to change it");
    (void)todayFill;
    int index = 0;
    const int count = (int)(sizeof GOAL_CHOICES / sizeof GOAL_CHOICES[0]);
    for (int i = 0; i < count; i++) if (GOAL_CHOICES[i] <= settings.dailyGoalMinutes) index = i;
    const bool sharing = shareCardOpen(); // the card over it has the keys
    const int change = sharing ? 0 : ImGui::IsKeyPressed(ImGuiKey_RightArrow) ? 1 : ImGui::IsKeyPressed(ImGuiKey_LeftArrow) ? -1 : 0;
    if (change != 0){
        const int next = std::clamp(index + change, 0, count - 1);
        if (GOAL_CHOICES[next] != settings.dailyGoalMinutes){
            settings.dailyGoalMinutes = GOAL_CHOICES[next];
            setDailyGoal(settings.dailyGoalMinutes);
            goalChanged = true;
        }
    }

    // The calendar: a column a week (Monday at the top), the last CALENDAR_WEEKS of them; each day as full as its
    // practice against the goal, a met goal lit whole; today ringed
    const int todayNumber = today();
    const int weekday = (todayNumber + 3) % 7; // 1970-01-01 was a Thursday: Monday 0
    const int firstDay = todayNumber - weekday - (CALENDAR_WEEKS - 1) * 7;
    const float calTop = goalTop + 52 * s, cell = std::min(18 * s, (middle - 60 * s - left) / CALENDAR_WEEKS - 3 * s), step = cell + 3 * s;
    const char* const DAYS[7] = { "M", "", "W", "", "F", "", "S" };
    for (int d = 0; d < 7; d++) draw->AddText(fonts.mono, 10 * s, ImVec2(left, calTop + d * step + 2 * s), uiColor(UiColor::Dim), DAYS[d]);
    const char* const MONTHS[12] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
    int lastMonth = -1;
    const ImVec2 mouse = ImGui::GetMousePos();
    std::string hoveredDay;
    for (int w = 0; w < CALENDAR_WEEKS; w++){
        const float x = left + 20 * s + w * step;
        int y0, m0, d0;
        dateFromDays(firstDay + w * 7, y0, m0, d0);
        if (m0 != lastMonth){
            draw->AddText(fonts.mono, 10 * s, ImVec2(x, calTop - 16 * s), uiColor(UiColor::Dim), MONTHS[m0 - 1]);
            lastMonth = m0;
        }
        for (int d = 0; d < 7; d++){
            const int day = firstDay + w * 7 + d;
            if (day > todayNumber) continue;
            const ImVec2 a(x, calTop + d * step), b(x + cell, calTop + d * step + cell);
            auto found = p.days.find(dateText(day));
            ImU32 color = uiColor(UiColor::StaffLine, 0.6f);
            if (found != p.days.end()){
                const float share = std::min(1.0f, found->second.seconds / (settings.dailyGoalMinutes * 60.0f));
                color = found->second.goalMet ? uiColor(UiColor::Accent) : uiColor(UiColor::Accent, 0.2f + 0.5f * share);
            }
            draw->AddRectFilled(a, b, color, 3 * s);
            const bool frozen = p.frozenDays.count(dateText(day)) > 0; // missed, but a freeze kept the streak: ringed
            if (frozen) draw->AddRect(ImVec2(a.x + 1 * s, a.y + 1 * s), ImVec2(b.x - 1 * s, b.y - 1 * s), uiColor(UiColor::Accent), 3 * s, 0, 2 * s);
            if (day == todayNumber) draw->AddRect(ImVec2(a.x - 2 * s, a.y - 2 * s), ImVec2(b.x + 2 * s, b.y + 2 * s), uiColor(UiColor::Ink), 4 * s, 0, 1.5f * s);
            if (mouse.x >= a.x && mouse.x < b.x && mouse.y >= a.y && mouse.y < b.y){
                hoveredDay = dateText(day) + (found == p.days.end() ? std::string(": no practice")
                                                                    : ": " + durationText(found->second.seconds) + ", " + thousands(found->second.xp) + " XP"
                                                                      + (found->second.goalMet ? ", goal met" : ""))
                             + (frozen ? "  ·  a streak freeze covered it" : "");
            }
        }
    }
    const float calBottom = calTop + 7 * step;
    draw->AddText(fonts.text, 13 * s, ImVec2(left, calBottom + 8 * s), uiColor(UiColor::Dim),
                  hoveredDay.empty() ? "Each day as full as its practice; lit whole when the goal was met, ringed when a freeze kept the streak"
                                     : hoveredDay.c_str());

    // This week so far against last week's same days (weeks from Monday): practice, XP, the days played
    float weekSeconds[2] = {}, weekDays[2] = {};
    long long weekXp[2] = {};
    const int thisMonday = todayNumber - weekday;
    for (int w = 0; w < 2; w++){
        for (int d = 0; d <= weekday; d++){
            auto found = p.days.find(dateText(thisMonday - w * 7 + d));
            if (found == p.days.end()) continue;
            weekSeconds[w] += found->second.seconds;
            weekXp[w] += found->second.xp;
            if (found->second.seconds > 0.0f) weekDays[w]++;
        }
    }
    const float weekTop = calBottom + 36 * s;
    draw->AddText(fonts.mono, 12 * s, ImVec2(left, weekTop), uiColor(UiColor::Dim), "THIS WEEK");
    auto trend = [&](float now, float before, const std::string& text, float x){
        const UiColor color = now > before ? UiColor::Good : now < before ? UiColor::Bad : UiColor::Dim;
        const char* arrow = now > before ? "up" : now < before ? "down" : "same";
        draw->AddText(fonts.bold, 16 * s, ImVec2(x, weekTop + 18 * s), uiColor(UiColor::Ink), text.c_str());
        draw->AddText(fonts.mono, 11 * s, ImVec2(x, weekTop + 40 * s), uiColor(color), TextFormat("%s on last week so far", arrow));
    };
    const float column = (middle - 30 * s - left) / 3;
    trend(weekSeconds[0], weekSeconds[1], durationText(weekSeconds[0]), left);
    trend((float)weekXp[0], (float)weekXp[1], thousands(weekXp[0]) + " XP", left + column);
    trend(weekDays[0], weekDays[1], TextFormat("%d of %d days", (int)weekDays[0], weekday + 1), left + 2 * column);

    // --- Right: the notes to review ---
    const float rightX = middle + 20 * s;
    float y = top;
    draw->AddText(fonts.mono, 12 * s, ImVec2(rightX, y), uiColor(UiColor::Dim), "NOTES TO REVIEW, THE LAST TWO WEEKS");
    y += 22 * s;
    const std::vector<NoteTally> weak = weakestNotes(p, REVIEW_NOTES);
    if (weak.empty()){
        draw->AddText(fonts.text, 15 * s, ImVec2(rightX, y), uiColor(UiColor::Dim),
                      p.recentNotes.empty() ? "Play some drills: the notes you miss most show up here" : "None missed often lately: well read!");
        y += 26 * s;
    }
    for (const NoteTally& note : weak){
        const std::string name = std::string(pitchClassName(note.pitch)) + std::to_string(pitchOctave(note.pitch));
        draw->AddText(fonts.bold, 16 * s, ImVec2(rightX, y), uiColor(UiColor::Ink), name.c_str());
        const float barX = rightX + 52 * s, barW = (right - barX) - 110 * s, share = (float)note.right / (float)note.asked;
        draw->AddRectFilled(ImVec2(barX, y + 6 * s), ImVec2(barX + barW, y + 14 * s), uiColor(UiColor::StaffLine), 4 * s);
        draw->AddRectFilled(ImVec2(barX, y + 6 * s), ImVec2(barX + barW * share, y + 14 * s), uiColor(share < 0.7f ? UiColor::Bad : UiColor::Accent), 4 * s);
        draw->AddText(fonts.mono, 12 * s, ImVec2(barX + barW + 10 * s, y + 2 * s), uiColor(UiColor::Dim), TextFormat("%d of %d", note.right, note.asked));
        y += 26 * s;
    }

    // --- Right: the achievements, a medal each ---
    y = std::max(y + 18 * s, top + 22 * s + REVIEW_NOTES * 26 * s + 18 * s);
    const std::vector<Achievement>& all = achievements();
    draw->AddText(fonts.mono, 12 * s, ImVec2(rightX, y), uiColor(UiColor::Dim),
                  TextFormat("ACHIEVEMENTS   %d OF %d", (int)p.unlocked.size(), (int)all.size()));
    y += 24 * s;
    const float cellWidth = (right - rightX) / MEDALS_A_ROW, radius = std::min(20 * s, cellWidth * 0.36f);
    int hovered = -1;
    for (int i = 0; i < (int)all.size(); i++){
        const ImVec2 c(rightX + (i % MEDALS_A_ROW + 0.5f) * cellWidth, y + (i / MEDALS_A_ROW) * (2 * radius + 14 * s) + radius);
        const bool unlocked = p.isUnlocked(i);
        drawMedal(draw, c, radius, all[(size_t)i].tier, unlocked);
        if (!unlocked){ // how far along: an arc round it
            const float share = std::min(1.0f, (float)p.metrics[(int)all[(size_t)i].metric] / (float)all[(size_t)i].goal);
            if (share > 0.0f){
                draw->PathArcTo(c, radius + 3 * s, -1.5707963f, -1.5707963f + 6.2831853f * share, 32);
                draw->PathStroke(uiColor(UiColor::Accent, 0.8f), 0, 2.5f * s);
            }
        }
        if ((mouse.x - c.x) * (mouse.x - c.x) + (mouse.y - c.y) * (mouse.y - c.y) <= (radius + 4 * s) * (radius + 4 * s)) hovered = i;
    }
    const int rows = ((int)all.size() + MEDALS_A_ROW - 1) / MEDALS_A_ROW;
    y += rows * (2 * radius + 14 * s) + 6 * s;
    // The one under the mouse: what it is, and when it was earned or how far along it is
    if (hovered >= 0){
        const Achievement& a = all[(size_t)hovered];
        std::string status;
        for (const Unlock& unlock : p.unlocked) if (unlock.achievement == hovered) status = "Earned " + unlock.date;
        if (status.empty()) status = thousands(std::min(a.goal, p.metrics[(int)a.metric])) + " / " + thousands(a.goal);
        draw->AddText(fonts.bold, 16 * s, ImVec2(rightX, y), uiColor(UiColor::Ink), a.name);
        draw->AddText(fonts.text, 14 * s, ImVec2(rightX, y + 20 * s), uiColor(UiColor::Dim), (std::string(a.description) + "   ·   " + status).c_str());
    } else {
        draw->AddText(fonts.text, 14 * s, ImVec2(rightX, y), uiColor(UiColor::Dim), "Point at a medal to see what it's for");
    }

    menuScreenHint("Left / Right  daily goal    S  share    Esc  back", s);
    if (!sharing && ImGui::IsKeyPressed(ImGuiKey_S, false)) openShareCard();
    drawShareCard(settings.playerName, s);
    ImGui::Dummy(ImVec2(1, 1));
    ImGui::End();
    return goalChanged;
}
