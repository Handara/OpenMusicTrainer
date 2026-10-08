#include "ui/sharecard.h"

#include "app/playerprogress.h"
#include "core/routine.h"
#include "imgui.h"
#include "raylib.h"
#include "ui/rewards.h"
#include "ui/theme.h"
#include "ui/ui.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <system_error>

const float CARD_RATIO = 1.91f;     // a link preview's (1200 by 630)
const float SAVED_SHOWN_S = 4.0f;   // "saved to ..." stays this long

static struct {
    bool open = false;
    bool saveAsked = false;
    ImVec4 rect{};          // where the card was drawn this frame: x, y, width, height
    std::string savedPath;  // the last picture saved, and when
    double savedAt = -100.0;
    std::string error;
} card;

void openShareCard(){
    card.open = true;
    card.saveAsked = false;
    card.error.clear();
}

void closeShareCard(){
    card.open = false;
    card.saveAsked = false;
}

bool shareCardOpen(){
    return card.open;
}

namespace {

std::string dateText(int day){
    int y, m, d;
    dateFromDays(day, y, m, d);
    char text[16];
    std::snprintf(text, sizeof text, "%04d-%02d-%02d", y, m, d);
    return text;
}

std::string hoursText(float seconds){
    const int minutes = (int)(seconds / 60.0f);
    return minutes < 60 ? std::to_string(minutes) + " min" : std::to_string(minutes / 60) + " h " + std::to_string(minutes % 60) + " min";
}

} // namespace

void drawShareCard(const std::string& name, float s){
    if (!card.open) return; // (Esc closes it: main's back key, so it isn't taken for leaving the profile too)
    if (ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false)) card.saveAsked = true;
    if (ImGui::IsKeyPressed(ImGuiKey_O, false) && !card.savedPath.empty())
        openFolder(std::filesystem::path(card.savedPath).parent_path().string());

    const PlayerProfile& p = playerProfile();
    const UiFonts& fonts = uiFonts();
    ImDrawList* draw = ImGui::GetForegroundDrawList();
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    draw->AddRectFilled(ImVec2(0, 0), display, uiColor(UiColor::Background, 0.92f));

    // The card: as wide as fits with room for the hint under it, at a link preview's shape
    const float width = std::min(display.x * 0.8f, (display.y * 0.78f) * CARD_RATIO), height = width / CARD_RATIO;
    const ImVec2 a((display.x - width) / 2, (display.y - height) / 2 - 16 * s), b(a.x + width, a.y + height);
    card.rect = ImVec4(a.x, a.y, width, height);
    const float u = width / 1200.0f; // the card's own unit: drawn the same whatever the window
    draw->AddRectFilled(a, b, uiColor(UiColor::Card), 18 * u);
    // An accent glow along the top, and a line under it
    draw->AddRectFilledMultiColor(a, ImVec2(b.x, a.y + 160 * u), uiColor(UiColor::Accent, 0.16f), uiColor(UiColor::Accent, 0.04f),
                                  uiColor(UiColor::Accent, 0.0f), uiColor(UiColor::Accent, 0.0f));
    draw->AddRect(a, b, uiColor(UiColor::Accent, 0.5f), 18 * u, 0, 2 * u);
    const float pad = 56 * u;
    float x = a.x + pad, y = a.y + pad;

    // lahn, and the day
    draw->AddText(fonts.heavy, 34 * u, ImVec2(x, y - 6 * u), uiColor(UiColor::Ink), "lahn");
    const std::string day = dateText(today());
    const ImVec2 dayExtent = fonts.mono->CalcTextSizeA(20 * u, FLT_MAX, 0.0f, day.c_str());
    draw->AddText(fonts.mono, 20 * u, ImVec2(b.x - pad - dayExtent.x, y), uiColor(UiColor::Dim), day.c_str());
    y += 70 * u;

    // The level's badge, the name, the level's title
    const ImVec2 badge(x + 62 * u, y + 62 * u);
    draw->AddCircleFilled(badge, 62 * u, uiColor(UiColor::Accent), 64);
    const std::string level = std::to_string(p.level.level);
    const ImVec2 levelExtent = fonts.heavy->CalcTextSizeA(56 * u, FLT_MAX, 0.0f, level.c_str());
    draw->AddText(fonts.heavy, 56 * u, ImVec2(badge.x - levelExtent.x / 2, badge.y - levelExtent.y / 2), uiColor(UiColor::Background), level.c_str());
    const float textX = x + 150 * u;
    draw->AddText(fonts.heavy, 58 * u, ImVec2(textX, y - 4 * u), uiColor(UiColor::Ink), name.empty() ? "A lahn player" : name.c_str());
    draw->AddText(fonts.bold, 28 * u, ImVec2(textX, y + 66 * u), uiColor(UiColor::Accent), TextFormat("Level %d  ·  %s", p.level.level, p.level.title));
    y += 170 * u;

    // The numbers
    struct Stat { const char* label; std::string value; bool lit; };
    const Stat stats[] = {
        { "STREAK", TextFormat("%d %s", p.streak, p.streak == 1 ? "day" : "days"), p.streak > 0 },
        { "PRACTICED", hoursText(p.totalSeconds), false },
        { "NOTES KNOWN", std::to_string((int)p.metrics[(int)Metric::NotesKnown]), false },
        { "RANKED", TextFormat("%.0f pp", p.totalPp), p.totalPp > 0.0f },
    };
    const float statWidth = (width - 2 * pad) / 4;
    for (int i = 0; i < 4; i++){
        const float sx = x + i * statWidth;
        draw->AddText(fonts.mono, 18 * u, ImVec2(sx, y), uiColor(UiColor::Dim), stats[i].label);
        draw->AddText(fonts.heavy, 44 * u, ImVec2(sx, y + 26 * u), uiColor(stats[i].lit ? UiColor::Accent : UiColor::Ink), stats[i].value.c_str());
    }
    y += 112 * u;

    // The latest achievements, as their medals; this week's practice as bars at the right
    const std::vector<Achievement>& all = achievements();
    const int shown = std::min(4, (int)p.unlocked.size());
    draw->AddText(fonts.mono, 18 * u, ImVec2(x, y), uiColor(UiColor::Dim),
                  TextFormat("%d OF %d ACHIEVEMENTS", (int)p.unlocked.size(), (int)all.size()));
    for (int i = 0; i < shown; i++){
        const Unlock& unlock = p.unlocked[p.unlocked.size() - 1 - (size_t)i];
        const ImVec2 c(x + 30 * u + i * 76 * u, y + 66 * u);
        drawMedal(draw, c, 28 * u, all[(size_t)unlock.achievement].tier, true);
    }
    if (shown > 0){
        const Achievement& latest = all[(size_t)p.unlocked.back().achievement];
        draw->AddText(fonts.bold, 22 * u, ImVec2(x + shown * 76 * u + 8 * u, y + 44 * u), uiColor(UiColor::Ink), latest.name);
        draw->AddText(fonts.text, 18 * u, ImVec2(x + shown * 76 * u + 8 * u, y + 72 * u), uiColor(UiColor::Dim), latest.description);
    }
    // The last seven days: minutes each, lit where the goal was met
    const float barsRight = b.x - pad, barsWidth = 260 * u, barGap = 10 * u, barWidth = (barsWidth - 6 * barGap) / 7;
    const float barsBottom = y + 100 * u, barsTop = y + 30 * u;
    draw->AddText(fonts.mono, 18 * u, ImVec2(barsRight - barsWidth, y), uiColor(UiColor::Dim), "THIS WEEK");
    float most = (float)p.goalMinutes;
    for (int k = 0; k < 7; k++){
        auto found = p.days.find(dateText(today() - 6 + k));
        if (found != p.days.end()) most = std::max(most, found->second.seconds / 60.0f);
    }
    for (int k = 0; k < 7; k++){
        auto found = p.days.find(dateText(today() - 6 + k));
        const float minutes = found == p.days.end() ? 0.0f : found->second.seconds / 60.0f;
        const bool met = found != p.days.end() && found->second.goalMet;
        const float bx = barsRight - barsWidth + k * (barWidth + barGap);
        const float top = barsBottom - std::max(4 * u, (barsBottom - barsTop) * minutes / most);
        draw->AddRectFilled(ImVec2(bx, top), ImVec2(bx + barWidth, barsBottom), uiColor(met ? UiColor::Accent : UiColor::StaffLine), 4 * u);
    }

    // At the foot: the totals, and what lahn is
    auto count = [](long long n, const char* one, const char* many){ return std::to_string(n) + " " + (n == 1 ? one : many); };
    const std::string totals = count(p.metrics[(int)Metric::NotesRight], "note right", "notes right") + "  ·  best streak "
                             + count(p.metrics[(int)Metric::BestStreak], "day", "days") + "  ·  "
                             + count(p.metrics[(int)Metric::SongsPlayed], "song played", "songs played") + "  ·  "
                             + count(p.metrics[(int)Metric::ChaptersPassed], "chapter passed", "chapters passed");
    const float footY = b.y - pad - 6 * u;
    draw->AddLine(ImVec2(x, footY - 22 * u), ImVec2(b.x - pad, footY - 22 * u), uiColor(UiColor::StaffLine), 1.5f * u);
    draw->AddText(fonts.text, 20 * u, ImVec2(x, footY), uiColor(UiColor::Dim), totals.c_str());
    const char* tagline = "learning music with lahn";
    const ImVec2 taglineExtent = fonts.bold->CalcTextSizeA(20 * u, FLT_MAX, 0.0f, tagline);
    draw->AddText(fonts.bold, 20 * u, ImVec2(b.x - pad - taglineExtent.x, footY), uiColor(UiColor::Accent), tagline);

    // Under the card (not in the picture): what the keys do, and where it was saved
    const float hintY = b.y + 18 * s;
    const bool justSaved = GetTime() - card.savedAt < SAVED_SHOWN_S && !card.savedPath.empty();
    const std::string hint = !card.error.empty() ? "Couldn't save it: " + card.error
                           : justSaved ? "Saved: " + card.savedPath + "    O  open the folder    Esc  close"
                                       : "Enter  save it as a picture    Esc  close";
    const ImVec2 hintExtent = fonts.text->CalcTextSizeA(16 * s, FLT_MAX, 0.0f, hint.c_str());
    draw->AddText(fonts.text, 16 * s, ImVec2((display.x - hintExtent.x) / 2, hintY),
                  uiColor(!card.error.empty() ? UiColor::Bad : justSaved ? UiColor::Accent : UiColor::Dim), hint.c_str());
}

void saveShareCardIfAsked(const std::string& sharesFolder){
    if (!card.open || !card.saveAsked) return;
    card.saveAsked = false;
    // The frame as drawn, before it's shown: the card's part of it, in the framebuffer's pixels
    Image frame = LoadImageFromScreen();
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    const float sx = display.x > 0 ? frame.width / display.x : 1.0f, sy = display.y > 0 ? frame.height / display.y : 1.0f;
    // In whole pixels, inside the frame (raylib's crop copies a row too many for a rectangle between pixels)
    const int left = std::clamp((int)std::lround(card.rect.x * sx), 0, frame.width - 1);
    const int top = std::clamp((int)std::lround(card.rect.y * sy), 0, frame.height - 1);
    const int wide = std::clamp((int)std::lround(card.rect.z * sx), 1, frame.width - left);
    const int high = std::clamp((int)std::lround(card.rect.w * sy), 1, frame.height - top);
    ImageCrop(&frame, Rectangle{ (float)left, (float)top, (float)wide, (float)high });
    std::error_code ec;
    std::filesystem::create_directories(sharesFolder, ec);
    std::string path;
    for (int n = 1; n < 1000; n++){ // lahn-2026-10-08.png, then -2, -3...
        path = (std::filesystem::path(sharesFolder) / (n == 1 ? "lahn-" + dateText(today()) + ".png"
                                                             : TextFormat("lahn-%s-%d.png", dateText(today()).c_str(), n))).string();
        if (!std::filesystem::exists(path, ec)) break;
    }
    if (ExportImage(frame, path.c_str())){
        card.savedPath = path;
        card.savedAt = GetTime();
        card.error.clear();
    } else card.error = "the picture couldn't be written to " + sharesFolder;
    UnloadImage(frame);
}
