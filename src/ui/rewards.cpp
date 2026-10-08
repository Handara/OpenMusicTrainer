#include "ui/rewards.h"

#include "app/playerprogress.h"
#include "audio/audio.h"
#include "core/music.h"
#include "raylib.h"
#include "ui/theme.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <deque>
#include <string>

const double XP_SHOWN_S = 2.4;   // the XP pill, after its last XP
const double CARD_S = 3.4;       // each card
const double SLIDE_S = 0.28;     // its slide in, and out

namespace {
struct RewardsShown {
    long long xp = 0;          // the XP pill's sum, while it's up
    double xpAt = -100.0;
    float barFrom = 0.0f;      // the level bar, where it was before this XP
    std::deque<Reward> cards;
    Reward card;
    double cardAt = -100.0;    // when the card now shown came; < 0: none
    bool showing = false;
};
}
static RewardsShown shown;

ImU32 tierColor(Tier tier, float alpha){
    switch (tier){
        case Tier::Bronze: return IM_COL32(205, 127, 50, (int)(255 * alpha));
        case Tier::Silver: return IM_COL32(200, 208, 220, (int)(255 * alpha));
        default: return IM_COL32(255, 205, 64, (int)(255 * alpha));
    }
}

// A five-pointed star
void drawStar(ImDrawList* draw, ImVec2 c, float radius, ImU32 color){
    ImVec2 points[10];
    for (int i = 0; i < 10; i++){
        const float angle = -1.5707963f + i * 3.14159265f / 5.0f, r = i % 2 ? radius * 0.45f : radius;
        points[i] = ImVec2(c.x + std::cos(angle) * r, c.y + std::sin(angle) * r);
    }
    for (int i = 0; i < 10; i += 2){ // a triangle each side of each point, from the centre: a star isn't convex
        ImVec2 tri[3] = { c, points[i], points[(i + 1) % 10] };
        draw->AddConvexPolyFilled(tri, 3, color);
        ImVec2 back[3] = { c, points[(i + 9) % 10], points[i] };
        draw->AddConvexPolyFilled(back, 3, color);
    }
}

void drawMedal(ImDrawList* draw, ImVec2 c, float radius, Tier tier, bool unlocked, float alpha){
    const ImU32 metal = unlocked ? tierColor(tier, alpha) : uiColor(UiColor::StaffLine, alpha);
    draw->AddCircleFilled(c, radius, metal, 40);
    draw->AddCircle(c, radius * 0.8f, unlocked ? IM_COL32(255, 255, 255, (int)(90 * alpha)) : uiColor(UiColor::Dim, 0.4f * alpha), 40, radius * 0.08f);
    drawStar(draw, c, radius * 0.5f, unlocked ? IM_COL32(255, 255, 255, (int)(235 * alpha)) : uiColor(UiColor::Background, 0.8f * alpha));
}

static float rewardVolume = 1.0f;

void setRewardVolume(float volume){
    rewardVolume = std::clamp(volume, 0.0f, 1.0f);
}

// Its jingle, on the game's keys
static void chime(Reward::Kind kind){
    const double at = audioTime() + 0.03;
    auto note = [&](int pitch, double after, float seconds, float volume){
        playBuiltInNoteAt("keys", midiToFrequency((float)pitch), at + after, seconds, volume * rewardVolume);
    };
    switch (kind){
        case Reward::Kind::Level: { // up the major chord, and the octave on top, held
            const int arpeggio[4] = { 0, 4, 7, 12 };
            for (int i = 0; i < 4; i++) note(72 + arpeggio[i], i * 0.09, 0.5f, 0.45f);
            for (int pitch : { 72, 76, 79, 84 }) note(pitch, 0.4, 1.4f, 0.3f);
            break;
        }
        case Reward::Kind::Achievement:
            note(79, 0.0, 0.5f, 0.45f);
            note(84, 0.12, 1.0f, 0.45f);
            note(88, 0.12, 1.0f, 0.25f);
            break;
        case Reward::Kind::Chapter: // a little fanfare: up the chord, twice
            for (int i = 0; i < 3; i++) note(72 + (i == 0 ? 0 : i == 1 ? 4 : 7), i * 0.08, 0.4f, 0.4f);
            for (int pitch : { 76, 79, 84 }) note(pitch, 0.32, 1.1f, 0.3f);
            break;
        case Reward::Kind::Freeze: // bright and high, like ice
            note(88, 0.0, 0.5f, 0.3f);
            note(91, 0.08, 0.5f, 0.3f);
            note(96, 0.16, 1.0f, 0.25f);
            break;
        case Reward::Kind::Goal:
            note(76, 0.0, 0.4f, 0.4f);
            note(79, 0.1, 0.4f, 0.4f);
            note(84, 0.2, 0.9f, 0.4f);
            break;
        default: break;
    }
}

void drawRewards(float s){
    const double now = GetTime();
    Reward reward;
    while (takeReward(reward)){
        if (reward.kind == Reward::Kind::Xp){
            const PlayerProfile& profile = playerProfile();
            if (now - shown.xpAt > XP_SHOWN_S){ // a new pill: its bar starts where the level was before
                shown.xp = 0;
                const long long before = profile.xp - reward.xp;
                const LevelInfo level = levelFor(before);
                shown.barFrom = levelFor(before).level == profile.level.level ? (float)level.intoLevel / (float)level.forNext : 0.0f;
            }
            shown.xp += reward.xp;
            shown.xpAt = now;
        } else shown.cards.push_back(reward);
    }
    ImDrawList* draw = ImGui::GetForegroundDrawList();
    const UiFonts& fonts = uiFonts();
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    const float centre = display.x / 2;

    // The XP pill: "+64 XP", the level's bar filling under it
    const double xpAge = now - shown.xpAt;
    if (xpAge < XP_SHOWN_S){
        const float alpha = (float)std::min(1.0, std::min(xpAge / 0.15, (XP_SHOWN_S - xpAge) / 0.4));
        const float rise = (float)std::max(0.0, 1.0 - xpAge / 0.3) * 10 * s;
        const std::string text = "+" + std::to_string(shown.xp) + " XP";
        const ImVec2 size = fonts.bold->CalcTextSizeA(20 * s, FLT_MAX, 0.0f, text.c_str());
        const float width = std::max(size.x + 40 * s, 150 * s), top = 16 * s + rise;
        const ImVec2 a(centre - width / 2, top), b(centre + width / 2, top + 52 * s);
        draw->AddRectFilled(a, b, uiColor(UiColor::Card, 0.95f * alpha), 14 * s);
        draw->AddRect(a, b, uiColor(UiColor::Accent, 0.8f * alpha), 14 * s, 0, 1.5f * s);
        draw->AddText(fonts.bold, 20 * s, ImVec2(centre - size.x / 2, top + 7 * s), uiColor(UiColor::Accent, alpha), text.c_str());
        const LevelInfo& level = playerProfile().level;
        const float to = (float)level.intoLevel / (float)std::max(1LL, level.forNext);
        const float fill = shown.barFrom + (to - shown.barFrom) * (float)std::min(1.0, std::max(0.0, (xpAge - 0.2) / 0.8));
        const float barLeft = a.x + 16 * s, barRight = b.x - 16 * s, barY = top + 38 * s;
        draw->AddRectFilled(ImVec2(barLeft, barY), ImVec2(barRight, barY + 5 * s), uiColor(UiColor::StaffLine, alpha), 3 * s);
        draw->AddRectFilled(ImVec2(barLeft, barY), ImVec2(barLeft + (barRight - barLeft) * std::clamp(fill, 0.0f, 1.0f), barY + 5 * s), uiColor(UiColor::Accent, alpha), 3 * s);
    }

    // The cards, one at a time
    if (!shown.showing && !shown.cards.empty()){
        shown.card = shown.cards.front();
        shown.cards.pop_front();
        shown.cardAt = now;
        shown.showing = true;
        chime(shown.card.kind);
    }
    if (!shown.showing) return;
    const double age = now - shown.cardAt;
    if (age > CARD_S){
        shown.showing = false;
        return;
    }
    const float slide = (float)std::min(1.0, std::min(age / SLIDE_S, (CARD_S - age) / SLIDE_S));
    const float ease = slide * slide * (3.0f - 2.0f * slide), alpha = ease;
    const float width = 470 * s, height = 96 * s, top = -height + (height + 80 * s) * ease;
    const ImVec2 a(centre - width / 2, top), b(centre + width / 2, top + height);
    const Reward& card = shown.card;
    ImU32 edge = uiColor(UiColor::Accent, alpha);
    if (card.kind == Reward::Kind::Achievement) edge = tierColor(achievements()[(size_t)card.achievement].tier, alpha);
    draw->AddRectFilled(ImVec2(a.x, a.y + 4 * s), ImVec2(b.x, b.y + 4 * s), IM_COL32(0, 0, 0, (int)(90 * alpha)), 16 * s);
    draw->AddRectFilled(a, b, uiColor(UiColor::Card, 0.97f * alpha), 16 * s);
    draw->AddRect(a, b, edge, 16 * s, 0, 2 * s);
    // A burst behind the badge as it lands
    const ImVec2 badge(a.x + 52 * s, a.y + height / 2);
    if (age < 0.9){
        const float burst = (float)(age / 0.9);
        const ImU32 ring = (edge & ~IM_COL32_A_MASK) | ((ImU32)(160 * (1.0f - burst) * alpha) << IM_COL32_A_SHIFT);
        draw->AddCircle(badge, 30 * s + 40 * s * burst, ring, 48, 3 * s);
    }
    const float textX = a.x + 100 * s;
    auto label = [&](const char* text, float y){ draw->AddText(fonts.mono, 12 * s, ImVec2(textX, a.y + y), uiColor(UiColor::Dim, alpha), text); };
    auto big = [&](const std::string& text, float y, UiColor color){ draw->AddText(fonts.heavy, 26 * s, ImVec2(textX, a.y + y), uiColor(color, alpha), text.c_str()); };
    auto small = [&](const std::string& text, float y){ draw->AddText(fonts.text, 15 * s, ImVec2(textX, a.y + y), uiColor(UiColor::Ink, 0.85f * alpha), text.c_str(), nullptr, 0.0f); };
    switch (card.kind){
        case Reward::Kind::Level: {
            draw->AddCircleFilled(badge, 30 * s, uiColor(UiColor::Accent, alpha), 40);
            const std::string number = std::to_string(card.level);
            const ImVec2 size = fonts.heavy->CalcTextSizeA(28 * s, FLT_MAX, 0.0f, number.c_str());
            draw->AddText(fonts.heavy, 28 * s, ImVec2(badge.x - size.x / 2, badge.y - size.y / 2), uiColor(UiColor::Background, alpha), number.c_str());
            label("LEVEL UP", 16 * s);
            big("Level " + number, 32 * s, UiColor::Ink);
            small(levelFor(xpToReach(card.level)).title, 66 * s);
            break;
        }
        case Reward::Kind::Achievement: {
            const Achievement& achievement = achievements()[(size_t)card.achievement];
            drawMedal(draw, badge, 30 * s, achievement.tier, true, alpha);
            label("ACHIEVEMENT UNLOCKED", 16 * s);
            big(achievement.name, 32 * s, UiColor::Ink);
            small(achievement.description, 66 * s);
            break;
        }
        case Reward::Kind::Chapter: {
            // A big star in a disc, then its stars popping in one by one
            draw->AddCircleFilled(badge, 30 * s, uiColor(UiColor::Accent, alpha), 40);
            drawStar(draw, badge, 18 * s, uiColor(UiColor::Background, alpha));
            label(card.courseDone ? "COURSE COMPLETE" : card.unitDone ? "LEVEL COMPLETE" : "CHAPTER COMPLETE", 16 * s);
            std::string title = card.title;
            if (title.size() > 30) title = title.substr(0, 29) + "...";
            big(title, 32 * s, UiColor::Ink);
            const int shownStars = std::min(card.starsPossible, 15);
            for (int k = 0; k < shownStars; k++){
                const float appear = (float)((age - 0.35 - k * 0.12) / 0.15);
                const bool earned = k < card.stars;
                const float pop = earned ? std::clamp(appear, 0.0f, 1.0f) : 1.0f;
                if (pop <= 0.0f) continue;
                const float grow = earned ? 1.0f + 0.5f * std::max(0.0f, 1.0f - std::fabs(appear - 1.0f) * 2.0f) : 1.0f;
                drawStar(draw, ImVec2(textX + 8 * s + k * 18 * s, a.y + 74 * s), 7 * s * pop * grow, earned ? uiColor(UiColor::Accent, alpha) : uiColor(UiColor::StaffLine, alpha));
            }
            draw->AddText(fonts.mono, 12 * s, ImVec2(textX + 8 * s + shownStars * 18 * s, a.y + 67 * s), uiColor(UiColor::Dim, alpha),
                          TextFormat("%d / %d", card.stars, card.starsPossible));
            break;
        }
        case Reward::Kind::Freeze: {
            // A snowflake: three crossed strokes with little tips
            draw->AddCircleFilled(badge, 30 * s, uiColor(UiColor::Card, alpha), 40);
            draw->AddCircle(badge, 30 * s, uiColor(UiColor::Accent, alpha), 40, 2 * s);
            for (int k = 0; k < 3; k++){
                const float angle = 3.14159265f * k / 3.0f, c = std::cos(angle) * 18 * s, sn = std::sin(angle) * 18 * s;
                draw->AddLine(ImVec2(badge.x - c, badge.y - sn), ImVec2(badge.x + c, badge.y + sn), uiColor(UiColor::Accent, alpha), 3 * s);
            }
            label("STREAK FREEZE EARNED", 16 * s);
            big(card.streak > 1 ? std::to_string(card.streak) + " freezes held" : std::string("A freeze held"), 32 * s, UiColor::Ink);
            small("A day you miss is covered: your streak goes on", 66 * s);
            break;
        }
        case Reward::Kind::Goal: {
            // A full ring: the day's goal, done
            draw->AddCircle(badge, 26 * s, uiColor(UiColor::Good, alpha), 48, 6 * s);
            const std::string number = std::to_string(card.streak);
            const ImVec2 size = fonts.heavy->CalcTextSizeA(24 * s, FLT_MAX, 0.0f, number.c_str());
            draw->AddText(fonts.heavy, 24 * s, ImVec2(badge.x - size.x / 2, badge.y - size.y / 2), uiColor(UiColor::Good, alpha), number.c_str());
            label("DAILY GOAL MET", 16 * s);
            big(card.streak == 1 ? std::string("A streak begins") : std::to_string(card.streak) + "-day streak", 32 * s, UiColor::Good);
            small("Come back tomorrow to keep it going", 66 * s);
            break;
        }
        default: break;
    }
}
