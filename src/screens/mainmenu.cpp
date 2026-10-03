#include "screens/mainmenu.h"

#include "core/routine.h"
#include "core/today.h"
#include "imgui.h"
#include "raylib.h"
#include "ui/menulist.h"
#include "ui/theme.h"
#include "ui/ui.h"

#include <algorithm>

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
    {"Tuner", "3", MainMenuChoice::Tuner},
    {"Instrument", "4", MainMenuChoice::Instrument},
    {"Settings", "5", MainMenuChoice::Settings},
    {"Quit", "Esc", MainMenuChoice::Quit},
};
const int ITEM_COUNT = sizeof(ITEMS) / sizeof(ITEMS[0]);

static struct {
    MenuList list;
    std::vector<MenuRow> rows;      // made once from ITEMS
    TodaySummary today;             // read when the menu appears, not every frame
} menu;

// The "today" panel: a card on the right, level with the list
static void drawToday(ImDrawList* draw, ImVec2 topLeft, float width, float s){
    const UiFonts& fonts = uiFonts();
    const TodaySummary& today = menu.today;
    const float pad = 26 * s, labelSize = 13 * s, textSize = 19 * s;
    float x = topLeft.x + pad, y = topLeft.y + pad, inner = width - 2 * pad;
    auto label = [&](const char* text){
        draw->AddText(fonts.mono, labelSize, ImVec2(x, y), uiColor(UiColor::Dim), text);
        y += labelSize + 8 * s;
    };
    auto line = [&](ImFont* font, float size, UiColor color, const std::string& text){
        draw->AddText(font, size, ImVec2(x, y), uiColor(color), text.c_str(), nullptr, inner); // wraps inside the card
        y += font ? font->CalcTextSizeA(size, FLT_MAX, inner, text.c_str()).y : size;
    };

    // The card goes under the text, so it's drawn first: its height comes from the rows it always has
    float height = pad * 2 + (labelSize + 8 * s) * 3 + 46 * s + 22 * s + textSize * 3 + 26 * s;
    draw->AddRectFilled(ImVec2(topLeft.x, topLeft.y + 3 * s), ImVec2(topLeft.x + width, topLeft.y + height + 3 * s), uiColor(UiColor::Ink, 0.04f), 10 * s);
    draw->AddRectFilled(topLeft, ImVec2(topLeft.x + width, topLeft.y + height), uiColor(UiColor::Card), 10 * s);

    label("DAY STREAK");
    std::string days = std::to_string(today.streakDays);
    draw->AddText(fonts.heavy, 46 * s, ImVec2(x, y), uiColor(UiColor::Ink), days.c_str());
    float numberWidth = fonts.heavy ? fonts.heavy->CalcTextSizeA(46 * s, FLT_MAX, 0.0f, days.c_str()).x : 30 * s;
    draw->AddText(fonts.bold, 18 * s, ImVec2(x + numberWidth + 8 * s, y + 22 * s), uiColor(UiColor::Dim), today.streakDays == 1 ? "day" : "days");
    y += 46 * s + 22 * s;

    label("TODAY");
    if (today.hasRoutine){
        line(fonts.text, textSize, UiColor::Ink, today.routineTitle + TextFormat("  ·  %.0f min", today.routineMinutes));
        y += 6 * s;
        // Done or not: a full brass bar, or an empty track
        draw->AddRectFilled(ImVec2(x, y), ImVec2(x + inner, y + 4 * s), uiColor(UiColor::StaffLine), 2 * s);
        if (today.routineDoneToday) draw->AddRectFilled(ImVec2(x, y), ImVec2(x + inner, y + 4 * s), uiColor(UiColor::Accent), 2 * s);
        y += 12 * s;
        line(fonts.text, 15 * s, today.routineDoneToday ? UiColor::Good : UiColor::Dim, today.routineDoneToday ? "Done today" : "Not yet today");
    } else {
        line(fonts.text, textSize, UiColor::Dim, "Make a routine to practice every day");
    }
    y += 18 * s;

    label("NEXT GOAL");
    if (today.hasDrill) line(fonts.bold, textSize, UiColor::Accent, today.drillTitle + TextFormat(" at %d bpm", today.drillTempo));
    else line(fonts.text, textSize, UiColor::Dim, "Try a scale drill in Learn");
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

    drawToday(draw, ImVec2(width * 0.58f, menuTop), width * 0.35f, s);

    // The footer: what the game listens to, and which game this is
    float footY = height - 40 * s;
    std::string input = "IN  ·  " + (info.inputDevice.empty() ? std::string("default input") : info.inputDevice);
    draw->AddText(fonts.mono, FOOT_SIZE * s, ImVec2(left, footY), uiColor(UiColor::Dim), input.c_str());
    const char* version = "lahn 0.1";
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
