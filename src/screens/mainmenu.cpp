#include "screens/mainmenu.h"

#include "app/playerprogress.h"
#include "core/routine.h"
#include "core/today.h"
#include "imgui.h"
#include "raylib.h"
#include "ui/menulist.h"
#include "ui/theme.h"
#include "ui/ui.h"

#include <algorithm>
#include <cfloat>
#include <string>

const float WORDMARK_SIZE = 40.0f;   // at a 720-pixel-tall window; everything scales with the window's height
const float FOOT_SIZE = 14.0f;
const float ROW_HEIGHT = 48.0f;      // the list's rows (ui/menulist), for lining the staff up with them

struct Item {
    const char* label;
    const char* key;    // the shortcut, as shown
    MainMenuChoice choice;
};
const Item ITEMS[] = {
    {"Play", "1", MainMenuChoice::Play},       // and practise, and edit songs (the song list's mode)
    {"Learn", "2", MainMenuChoice::Learn},     // and make lessons
    {"Profile", "3", MainMenuChoice::Profile}, // level, stats, achievements
    {"Tuner", "4", MainMenuChoice::Tuner},
    {"Instrument", "5", MainMenuChoice::Instrument},
    {"Settings", "6", MainMenuChoice::Settings},
    {"Quit", "Esc", MainMenuChoice::Quit},
};
const int ITEM_COUNT = sizeof(ITEMS) / sizeof(ITEMS[0]);

static struct {
    MenuList list;
    std::vector<MenuRow> rows;      // made once from ITEMS
    TodaySummary today;             // read when the menu appears, not every frame
} menu;

// The player's card, on the right, level with the list: their level and its bar, today's practice against the daily
// goal (a ring filling) and the streak, then the next thing to do
static void drawPlayerCard(ImDrawList* draw, ImVec2 topLeft, float width, float s){
    const UiFonts& fonts = uiFonts();
    const TodaySummary& today = menu.today;
    const PlayerProfile& p = playerProfile();
    const float pad = 24 * s, inner = width - 2 * pad, height = 300 * s;
    draw->AddRectFilled(ImVec2(topLeft.x, topLeft.y + 3 * s), ImVec2(topLeft.x + width, topLeft.y + height + 3 * s), uiColor(UiColor::Ink, 0.04f), 10 * s);
    draw->AddRectFilled(topLeft, ImVec2(topLeft.x + width, topLeft.y + height), uiColor(UiColor::Card), 10 * s);
    const float x = topLeft.x + pad;
    float y = topLeft.y + pad;
    auto centred = [&](ImFont* font, float size, ImVec2 c, ImU32 color, const std::string& text){
        const ImVec2 extent = font->CalcTextSizeA(size, FLT_MAX, 0.0f, text.c_str());
        draw->AddText(font, size, ImVec2(c.x - extent.x / 2, c.y - extent.y / 2), color, text.c_str());
    };

    // The level: its number in a disc, its title, its bar
    const float radius = 26 * s;
    const ImVec2 badge(x + radius, y + radius);
    draw->AddCircleFilled(badge, radius, uiColor(UiColor::Accent), 40);
    centred(fonts.heavy, 24 * s, badge, uiColor(UiColor::Background), std::to_string(p.level.level));
    const float textX = x + 2 * radius + 16 * s;
    draw->AddText(fonts.mono, 12 * s, ImVec2(textX, y), uiColor(UiColor::Dim), TextFormat("LEVEL %d", p.level.level));
    draw->AddText(fonts.bold, 20 * s, ImVec2(textX, y + 14 * s), uiColor(UiColor::Ink), p.level.title);
    const float fill = (float)p.level.intoLevel / (float)std::max(1LL, p.level.forNext), barRight = x + inner;
    draw->AddRectFilled(ImVec2(textX, y + 42 * s), ImVec2(barRight, y + 48 * s), uiColor(UiColor::StaffLine), 3 * s);
    draw->AddRectFilled(ImVec2(textX, y + 42 * s), ImVec2(textX + (barRight - textX) * fill, y + 48 * s), uiColor(UiColor::Accent), 3 * s);
    y += 2 * radius + 24 * s;

    // Today: a ring as full as today's practice against the goal, and the streak beside it
    const ImVec2 ring(x + radius, y + radius);
    const float share = std::min(1.0f, p.secondsToday / (p.goalMinutes * 60.0f));
    draw->AddCircle(ring, radius - 3 * s, uiColor(UiColor::StaffLine), 48, 6 * s);
    if (share > 0.0f){
        draw->PathArcTo(ring, radius - 3 * s, -1.5707963f, -1.5707963f + 6.2831853f * share, 48);
        draw->PathStroke(uiColor(p.goalMetToday ? UiColor::Good : UiColor::Accent), 0, 6 * s);
    }
    centred(fonts.bold, 15 * s, ring, uiColor(p.goalMetToday ? UiColor::Good : UiColor::Ink), std::to_string((int)(p.secondsToday / 60.0f)));
    draw->AddText(fonts.mono, 12 * s, ImVec2(textX, y), uiColor(UiColor::Dim), "TODAY");
    draw->AddText(fonts.bold, 18 * s, ImVec2(textX, y + 14 * s), uiColor(p.goalMetToday ? UiColor::Good : UiColor::Ink),
                  p.goalMetToday ? "Daily goal met" : TextFormat("%d of %d min", (int)(p.secondsToday / 60.0f), p.goalMinutes));
    draw->AddText(fonts.text, 15 * s, ImVec2(textX, y + 36 * s), uiColor(p.streak > 0 ? UiColor::Accent : UiColor::Dim),
                  p.streak > 0 ? TextFormat("%d-day streak%s", p.streak, p.goalMetToday ? "" : ": keep it today") : "Meet the goal to start a streak");
    y += 2 * radius + 26 * s;

    // Next: the drill to go on with (or a routine), and the profile's way in
    draw->AddText(fonts.mono, 12 * s, ImVec2(x, y), uiColor(UiColor::Dim), "NEXT GOAL");
    y += 18 * s;
    std::string next = today.hasDrill ? today.drillTitle + TextFormat(" at %d bpm", today.drillTempo)
                     : today.hasRoutine ? today.routineTitle + (today.routineDoneToday ? "  ·  done today" : TextFormat("  ·  %.0f min", today.routineMinutes))
                     : "Start a course in Learn";
    draw->AddText(fonts.bold, 17 * s, ImVec2(x, y), uiColor(UiColor::Accent), next.c_str(), nullptr, inner);
    y += fonts.bold->CalcTextSizeA(17 * s, FLT_MAX, inner, next.c_str()).y + 12 * s;
    draw->AddText(fonts.mono, 12 * s, ImVec2(x, topLeft.y + height - pad - 10 * s), uiColor(UiColor::Dim),
                  TextFormat("%d OF %d ACHIEVEMENTS  ·  PROFILE: 3", (int)p.unlocked.size(), (int)achievements().size()));
}

MainMenuChoice mainMenuScreen(const MainMenuInfo& info, const std::string& error){
    MainMenuChoice choice = MainMenuChoice::None;
    beginMenu("MainMenu");
    if (ImGui::IsWindowAppearing()){
        // Coming back to the menu: read the progress again, it may have changed
        std::vector<ExerciseEntry> exercises = scanExercises(info.builtInExercises, true);
        std::vector<ExerciseEntry> user = scanExercises(info.userExercises, false);
        exercises.insert(exercises.end(), user.begin(), user.end());
        checkRoutines(exercises);
        menu.today = summarizeToday(exercises, info.progressDir, today());
        refreshPlayerProgress(); // a new day: the streak, today's minutes
    }
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const UiFonts& fonts = uiFonts();
    const float width = ImGui::GetWindowWidth(), height = ImGui::GetWindowHeight();
    const float s = menuScale();
    const float left = width * 0.07f;
    const float menuTop = height * 0.25f;

    // The staff: five hairlines across the screen, one every two rows, from the top of the list to below its end
    for (int line = 0; line < 5; line++){
        float y = menuTop + line * 2 * ROW_HEIGHT * s;
        horizontalLine(draw, 0, width, y, 1.0f, uiColor(UiColor::StaffLine));
    }
    drawWordmark(draw, ImVec2(left, height * 0.09f), WORDMARK_SIZE * s);

    // The list, and its shortcuts: 1 to 7 go straight to an item; Esc moves to Quit, and quitting still takes Enter
    if (menu.rows.empty()){
        for (const Item& item : ITEMS){
            MenuRow row;
            row.label = item.label;
            row.key = item.key;
            menu.rows.push_back(row);
        }
    }
    int confirmed = menuList(menu.list, menu.rows, {ImVec2(left, menuTop), width * 0.45f, ITEM_COUNT * ROW_HEIGHT * s, s});
    for (int i = 0; i < ITEM_COUNT - 1; i++){
        if (ImGui::IsKeyPressed((ImGuiKey)(ImGuiKey_1 + i))){
            menuListSelect(menu.list, menu.rows, i, true);
            confirmed = i;
        }
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Escape)) menuListSelect(menu.list, menu.rows, ITEM_COUNT - 1);
    if (confirmed >= 0) choice = ITEMS[confirmed].choice;

    drawPlayerCard(draw, ImVec2(width * 0.58f, menuTop), width * 0.35f, s);

    // The footer: what the game listens to, and which game this is
    float footY = height - 40 * s;
    std::string input = "IN  ·  " + (info.inputDevice.empty() ? std::string("default input") : info.inputDevice);
    draw->AddText(fonts.mono, FOOT_SIZE * s, ImVec2(left, footY), uiColor(UiColor::Dim), input.c_str());
    const char* version = "hardthz 0.1";
    float versionWidth = fonts.mono ? fonts.mono->CalcTextSizeA(FOOT_SIZE * s, FLT_MAX, 0.0f, version).x : 60 * s;
    draw->AddText(fonts.mono, FOOT_SIZE * s, ImVec2(width - left - versionWidth, footY), uiColor(UiColor::Dim), version);

    if (!error.empty()){
        ImGui::SetCursorPos(ImVec2(left, menuTop + ITEM_COUNT * ROW_HEIGHT * s + 10 * s));
        ImGui::PushTextWrapPos(width * 0.55f);
        ImGui::TextColored(uiColorVec(UiColor::Bad), "%s", error.c_str());
        ImGui::PopTextWrapPos();
    }
    ImGui::End();
    return choice;
}
