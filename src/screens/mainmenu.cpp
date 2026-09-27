#include "screens/mainmenu.h"

#include "audio/audio.h"
#include "core/music.h"
#include "core/routine.h"
#include "core/today.h"
#include "imgui.h"
#include "raylib.h"
#include "ui/theme.h"
#include "ui/ui.h"

#include <algorithm>
#include <cmath>

// Sizes at a 720-pixel-tall window; everything scales with the window's height
const float REFERENCE_HEIGHT = 720.0f;
const float WORDMARK_SIZE = 40.0f;
const float ITEM_SIZE = 30.0f;
const float KEY_SIZE = 15.0f;
const float FOOT_SIZE = 14.0f;
const float ROW_HEIGHT = 48.0f;
const float KEY_COLUMN = 52.0f;        // the shortcut numbers' column, before the names
const float SELECTED_SHIFT = 12.0f;    // the selected name steps right, toward you
const float RING_SIZE = 7.0f;          // how far the string swings when plucked
const float RING_DECAY = 6.0f;         // per second
const float RING_SPEED = 55.0f;        // radians per second: a visible shimmer, not a real string's pitch
const float GLIDE_SPEED = 18.0f;       // how fast the string and the names follow the selection

struct Item {
    const char* label;
    const char* key;    // the shortcut, as shown
    MainMenuChoice choice;
};
const Item ITEMS[] = {
    {"Play", "1", MainMenuChoice::Play},
    {"Learn", "2", MainMenuChoice::Learn},
    {"Song editor", "3", MainMenuChoice::Editor},
    {"Lesson editor", "4", MainMenuChoice::LessonEditor},
    {"Tuner", "5", MainMenuChoice::Tuner},
    {"Settings", "6", MainMenuChoice::Settings},
    {"Quit", "Esc", MainMenuChoice::Quit},
};
const int ITEM_COUNT = sizeof(ITEMS) / sizeof(ITEMS[0]);

// A major pentatonic, climbing with the menu: no half steps, so any two items sound fine one after the other
const int ITEM_NOTES[ITEM_COUNT] = { 69, 71, 73, 76, 78, 81, 83 };

static struct {
    int selected = 0;
    float stringY = -1.0f;          // where the string is drawn: it glides to the selected row
    float shift[ITEM_COUNT] = {};   // each name's step to the right, easing in and out
    double ringStart = -100.0;      // when the string was last plucked
    float ringStrength = 1.0f;
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

static void select(int index){
    if (index == menu.selected) return;
    menu.selected = index;
    menu.ringStart = GetTime();
    menu.ringStrength = 1.0f;
    playPreview(midiToFrequency((float)ITEM_NOTES[index]));
}

// Confirming answers with the item's note and the fifth above it, a little after: an answer, not a beep
static MainMenuChoice confirm(int index){
    menu.ringStart = GetTime();
    menu.ringStrength = 2.0f;
    playPreview(midiToFrequency((float)ITEM_NOTES[index]));
    playPreview(midiToFrequency((float)(ITEM_NOTES[index] + 7)), 0.11f);
    return ITEMS[index].choice;
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
    const float s = std::clamp(height / REFERENCE_HEIGHT, 0.75f, 2.0f);
    const float left = width * 0.07f;
    const float menuTop = height * 0.25f;
    const float keyX = left, nameX = left + KEY_COLUMN * s;

    // The staff: five hairlines across the screen, one every two rows, from the top of the list to below its end
    for (int line = 0; line < 5; line++){
        float y = menuTop + line * 2 * ROW_HEIGHT * s;
        draw->AddLine(ImVec2(0, y), ImVec2(width, y), uiColor(UiColor::StaffLine), 1.0f);
    }
    drawWordmark(draw, ImVec2(left, height * 0.09f), WORDMARK_SIZE * s);

    // Input: the mouse hovers and clicks rows; the keyboard moves, confirms, and jumps with shortcuts
    ImVec2 mouse = ImGui::GetMousePos();
    bool mouseMoved = ImGui::GetIO().MouseDelta.x != 0.0f || ImGui::GetIO().MouseDelta.y != 0.0f;
    for (int i = 0; i < ITEM_COUNT; i++){
        float top = menuTop + i * ROW_HEIGHT * s;
        bool over = mouse.x >= keyX && mouse.x <= nameX + 340 * s && mouse.y >= top && mouse.y < top + ROW_HEIGHT * s;
        if (over && mouseMoved) select(i);
        if (over && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) choice = confirm(i);
    }
    if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) select((menu.selected + 1) % ITEM_COUNT);
    if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) select((menu.selected + ITEM_COUNT - 1) % ITEM_COUNT);
    if (ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter) || ImGui::IsKeyPressed(ImGuiKey_Space)){
        choice = confirm(menu.selected);
    }
    for (int i = 0; i < ITEM_COUNT - 1; i++){ // 1 to 6 go straight there
        if (ImGui::IsKeyPressed((ImGuiKey)(ImGuiKey_1 + i))){
            select(i);
            choice = confirm(i);
        }
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Escape)) select(ITEM_COUNT - 1); // Esc goes to Quit; quitting still takes Enter

    // The rows: the shortcut in its own column, then the name; the selected one in ink, stepped right
    float dt = std::min(GetFrameTime(), 0.05f);
    float follow = std::min(1.0f, dt * GLIDE_SPEED);
    for (int i = 0; i < ITEM_COUNT; i++){
        float top = menuTop + i * ROW_HEIGHT * s;
        bool selected = i == menu.selected;
        menu.shift[i] += ((selected ? SELECTED_SHIFT * s : 0.0f) - menu.shift[i]) * follow;
        float nameHeight = ITEM_SIZE * s;
        float nameY = top + (ROW_HEIGHT * s - nameHeight) / 2;
        draw->AddText(fonts.mono, KEY_SIZE * s, ImVec2(keyX, nameY + (nameHeight - KEY_SIZE * s) * 0.55f),
                      uiColor(UiColor::Dim, selected ? 1.0f : 0.7f), ITEMS[i].key);
        draw->AddText(fonts.bold, nameHeight, ImVec2(nameX + menu.shift[i], nameY),
                      uiColor(selected ? UiColor::Ink : UiColor::Dim), ITEMS[i].label);
    }

    // The string: from the screen's edge to just before the shortcut column, level with the selected row. It glides
    // to a new row and rings each time it's plucked (moved, or more strongly when confirmed).
    float targetY = menuTop + (menu.selected + 0.5f) * ROW_HEIGHT * s;
    menu.stringY = menu.stringY < 0 ? targetY : menu.stringY + (targetY - menu.stringY) * follow;
    float age = (float)(GetTime() - menu.ringStart);
    float amplitude = RING_SIZE * s * menu.ringStrength * std::exp(-age * RING_DECAY);
    float end = keyX - 14 * s;
    const int SEGMENTS = 48;
    ImVec2 points[SEGMENTS + 1];
    for (int k = 0; k <= SEGMENTS; k++){
        float u = (float)k / SEGMENTS;
        points[k] = ImVec2(end * u, menu.stringY + amplitude * std::sin(PI * u) * std::sin(age * RING_SPEED));
    }
    draw->AddPolyline(points, SEGMENTS + 1, uiColor(UiColor::Accent), ImDrawFlags_None, std::max(1.5f, 1.6f * s));
    draw->AddCircleFilled(ImVec2(end, menu.stringY), 3.2f * s, uiColor(UiColor::Accent));

    drawToday(draw, ImVec2(width * 0.58f, menuTop), width * 0.35f, s);

    // The footer: what the game listens to, and which game this is
    float footY = height - 40 * s;
    std::string input = "IN  ·  " + (info.inputDevice.empty() ? std::string("default input") : info.inputDevice);
    draw->AddText(fonts.mono, FOOT_SIZE * s, ImVec2(left, footY), uiColor(UiColor::Dim), input.c_str());
    const char* version = "lahn 0.1";
    float versionWidth = fonts.mono ? fonts.mono->CalcTextSizeA(FOOT_SIZE * s, FLT_MAX, 0.0f, version).x : 60 * s;
    draw->AddText(fonts.mono, FOOT_SIZE * s, ImVec2(width - left - versionWidth, footY), uiColor(UiColor::Dim), version);

    if (!error.empty()){
        ImGui::SetCursorPos(ImVec2(nameX, menuTop + ITEM_COUNT * ROW_HEIGHT * s + 10 * s));
        ImGui::PushTextWrapPos(width * 0.55f);
        ImGui::TextColored(uiColorVec(UiColor::Bad), "%s", error.c_str());
        ImGui::PopTextWrapPos();
    }
    ImGui::End();
    return choice;
}
