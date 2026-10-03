#include "ui/menulist.h"

#include "audio/audio.h"
#include "core/music.h"
#include "raylib.h"
#include "ui/theme.h"

#include <algorithm>
#include <cmath>

// Sizes at a 720-pixel-tall window
const float REFERENCE_HEIGHT = 720.0f;
const float ITEM_SIZE = 30.0f;
const float DETAIL_SIZE = 18.0f;
const float KEY_SIZE = 15.0f;
const float NOTE_SIZE = 14.0f;
const float HEADING_SIZE = 13.0f;
const float ROW_HEIGHT = 48.0f;
const float HEADING_HEIGHT = 40.0f;
const float NOTE_HEIGHT = 20.0f;
const float KEY_COLUMN = 52.0f;        // the shortcut keys' column, before the names
const float SELECTED_SHIFT = 12.0f;    // the selected name steps right, toward you
const float RING_SIZE = 7.0f;          // how far the string swings when plucked
const float RING_DECAY = 6.0f;         // per second
const float RING_SPEED = 55.0f;        // radians per second: a visible shimmer, not a real string's pitch
const float GLIDE_SPEED = 18.0f;       // how fast the string, the names and the scrolling follow
const float TITLE_SIZE = 40.0f;
const float HINT_SIZE = 14.0f;

// A major pentatonic, climbing with the list and starting over every seven rows: no half steps, so any path through
// a list sounds fine
const int NOTES[] = { 69, 71, 73, 76, 78, 81, 83 };

float menuScale(){
    return std::clamp(ImGui::GetIO().DisplaySize.y / REFERENCE_HEIGHT, 0.75f, 2.0f);
}

static bool selectable(const MenuRow& row){
    return !row.heading;
}

static float rowHeight(const MenuRow& row, float s){
    if (row.heading) return HEADING_HEIGHT * s;
    return (ROW_HEIGHT + (row.note.empty() ? 0.0f : NOTE_HEIGHT)) * s;
}

// The row's note: its place among the rows that can be selected
static int noteFor(const std::vector<MenuRow>& rows, int row){
    int place = 0;
    for (int i = 0; i < row; i++) if (selectable(rows[i])) place++;
    return NOTES[place % 7];
}

void menuListSelect(MenuList& list, const std::vector<MenuRow>& rows, int row, bool confirm){
    if (row < 0 || row >= (int)rows.size() || !selectable(rows[row])) return;
    bool moved = row != list.selected;
    list.selected = row;
    if (!moved && !confirm) return;
    list.ringStart = GetTime();
    list.ringStrength = confirm ? 2.0f : 1.0f;
    int note = noteFor(rows, row);
    playPreview(midiToFrequency((float)note));
    // Confirming answers with the fifth above, a little after: an answer, not a beep
    if (confirm) playPreview(midiToFrequency((float)(note + 7)), 0.11f);
}

// The next row that can be selected, going `step` (+1 or -1) from `from`, wrapping around
static int nextSelectable(const std::vector<MenuRow>& rows, int from, int step){
    int count = (int)rows.size();
    for (int k = 1; k <= count; k++){
        int i = ((from + step * k) % count + count) % count;
        if (selectable(rows[i])) return i;
    }
    return from;
}

int menuList(MenuList& list, const std::vector<MenuRow>& rows, const MenuListArea& area){
    if (rows.empty()) return -1;
    const float s = area.scale;
    const UiFonts& fonts = uiFonts();
    ImDrawList* draw = ImGui::GetWindowDrawList();
    ImGuiIO& io = ImGui::GetIO();
    list.shift.resize(rows.size(), 0.0f);
    if (list.selected < 0 || list.selected >= (int)rows.size() || !selectable(rows[list.selected])){
        list.selected = nextSelectable(rows, -1, 1);
    }

    bool anyKey = false;
    for (const MenuRow& row : rows) if (!row.key.empty()) anyKey = true;
    const float keyX = area.topLeft.x, nameX = keyX + (anyKey ? KEY_COLUMN * s : 0.0f);

    // Where each row sits, before scrolling
    std::vector<float> tops(rows.size());
    float total = 0.0f;
    for (size_t i = 0; i < rows.size(); i++){
        tops[i] = total;
        total += rowHeight(rows[i], s);
    }
    float maxScroll = std::max(0.0f, total - area.height);

    // Input: the keyboard (unless a text field has it), the mouse over the rows, the wheel
    int confirmed = -1;
    if (!io.WantTextInput){
        if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) menuListSelect(list, rows, nextSelectable(rows, list.selected, 1));
        if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) menuListSelect(list, rows, nextSelectable(rows, list.selected, -1));
        if (ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter) || ImGui::IsKeyPressed(ImGuiKey_Space)){
            if (!rows[list.selected].disabled){
                menuListSelect(list, rows, list.selected, true);
                confirmed = list.selected;
            }
        }
    }
    ImVec2 mouse = ImGui::GetMousePos();
    bool overArea = mouse.x >= 0 && mouse.x <= area.topLeft.x + area.width && mouse.y >= area.topLeft.y && mouse.y < area.topLeft.y + area.height;
    bool mouseMoved = io.MouseDelta.x != 0.0f || io.MouseDelta.y != 0.0f;
    if (overArea){
        list.scroll = std::clamp(list.scroll - io.MouseWheel * ROW_HEIGHT * s, 0.0f, maxScroll);
        for (size_t i = 0; i < rows.size(); i++){
            float top = area.topLeft.y + tops[i] - list.scroll;
            if (!selectable(rows[i]) || mouse.y < top || mouse.y >= top + rowHeight(rows[i], s) || mouse.x < keyX) continue;
            if (mouseMoved) menuListSelect(list, rows, (int)i);
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !rows[i].disabled){
                menuListSelect(list, rows, (int)i, true);
                confirmed = (int)i;
            }
        }
    }

    // Keep the selected row in view: scroll just enough, smoothly, when the keyboard moves past an edge
    float dt = std::min(GetFrameTime(), 0.05f);
    float follow = std::min(1.0f, dt * GLIDE_SPEED);
    float selTop = tops[list.selected], selBottom = selTop + rowHeight(rows[list.selected], s);
    float margin = ROW_HEIGHT * s;
    if (selTop - margin < list.scroll) list.scroll += (std::max(0.0f, selTop - margin) - list.scroll) * follow;
    if (selBottom + margin > list.scroll + area.height) list.scroll += (std::min(maxScroll, selBottom + margin - area.height) - list.scroll) * follow;

    // The rows, clipped to the area
    draw->PushClipRect(ImVec2(0, area.topLeft.y), ImVec2(area.topLeft.x + area.width, area.topLeft.y + area.height), true);
    for (size_t i = 0; i < rows.size(); i++){
        const MenuRow& row = rows[i];
        float top = area.topLeft.y + tops[i] - list.scroll;
        float height = rowHeight(row, s);
        if (top + height < area.topLeft.y || top > area.topLeft.y + area.height) continue;
        if (row.heading){
            // Headings: small capitals, like the labels on the main menu's card, near the bottom of their space
            draw->AddText(fonts.mono, HEADING_SIZE * s, ImVec2(keyX, top + height - HEADING_SIZE * s - 8 * s), uiColor(UiColor::Dim), row.label.c_str());
            continue;
        }
        bool selected = (int)i == list.selected;
        list.shift[i] += ((selected ? SELECTED_SHIFT * s : 0.0f) - list.shift[i]) * follow;
        float nameSize = ITEM_SIZE * s;
        float nameY = top + (ROW_HEIGHT * s - nameSize) / 2;
        UiColor nameColor = row.disabled ? UiColor::Dim : (selected ? UiColor::Ink : UiColor::Dim);
        float nameAlpha = row.disabled ? 0.55f : 1.0f;
        if (!row.key.empty()){
            draw->AddText(fonts.mono, KEY_SIZE * s, ImVec2(keyX, nameY + (nameSize - KEY_SIZE * s) * 0.55f),
                          uiColor(UiColor::Dim, selected ? 1.0f : 0.7f), row.key.c_str());
        }
        float x = nameX + list.shift[i];
        draw->AddText(fonts.bold, nameSize, ImVec2(x, nameY), uiColor(nameColor, nameAlpha), row.label.c_str());
        if (!row.detail.empty()){
            float labelWidth = fonts.bold ? fonts.bold->CalcTextSizeA(nameSize, FLT_MAX, 0.0f, row.label.c_str()).x : 0.0f;
            draw->AddText(fonts.text, DETAIL_SIZE * s, ImVec2(x + labelWidth + 14 * s, nameY + (nameSize - DETAIL_SIZE * s) * 0.6f),
                          uiColor(UiColor::Dim, selected ? 1.0f : 0.8f), row.detail.c_str());
        }
        if (!row.note.empty()){
            draw->AddText(fonts.text, NOTE_SIZE * s, ImVec2(nameX, top + ROW_HEIGHT * s - 4 * s), uiColor(UiColor::Bad),
                          row.note.c_str(), nullptr, area.width - (nameX - keyX));
        }
    }

    // The string: from the screen's edge to just before the list, level with the selected row. It glides to a new
    // row and rings each time it's plucked.
    float targetY = area.topLeft.y + tops[list.selected] - list.scroll + ROW_HEIGHT * s / 2;
    list.stringY = list.stringY < 0 ? targetY : list.stringY + (targetY - list.stringY) * follow;
    float age = (float)(GetTime() - list.ringStart);
    float amplitude = RING_SIZE * s * list.ringStrength * std::exp(-age * RING_DECAY);
    float end = keyX - 14 * s;
    const int SEGMENTS = 48;
    ImVec2 points[SEGMENTS + 1];
    for (int k = 0; k <= SEGMENTS; k++){
        float u = (float)k / SEGMENTS;
        points[k] = ImVec2(end * u, list.stringY + amplitude * std::sin(PI * u) * std::sin(age * RING_SPEED));
    }
    draw->AddPolyline(points, SEGMENTS + 1, uiColor(UiColor::Accent), ImDrawFlags_None, std::max(1.5f, 1.6f * s));
    draw->AddCircleFilled(ImVec2(end, list.stringY), 3.2f * s, uiColor(UiColor::Accent));
    draw->PopClipRect();
    return confirmed;
}

void menuScreenTitle(const char* title, float s){
    ImDrawList* draw = ImGui::GetWindowDrawList();
    float left = ImGui::GetWindowWidth() * 0.07f;
    draw->AddText(uiFonts().heavy, TITLE_SIZE * s, ImVec2(left, ImGui::GetWindowHeight() * 0.09f), uiColor(UiColor::Ink), title);
}

void menuScreenHint(const char* hint, float s){
    ImDrawList* draw = ImGui::GetWindowDrawList();
    float left = ImGui::GetWindowWidth() * 0.07f;
    draw->AddText(uiFonts().mono, HINT_SIZE * s, ImVec2(left, ImGui::GetWindowHeight() - 40 * s), uiColor(UiColor::Dim), hint);
}

bool menuPill(const char* text, const char* key, ImVec2 anchor, bool alignRight, int arrow, float s){
    ImDrawList* draw = ImGui::GetForegroundDrawList();
    const UiFonts& fonts = uiFonts();
    const float textSize = 16 * s, keySize = 12 * s, height = 30 * s, padding = 12 * s, chevron = 5 * s;
    float textWidth = fonts.bold ? fonts.bold->CalcTextSizeA(textSize, FLT_MAX, 0.0f, text).x : 40 * s;
    float keyWidth = key && fonts.mono ? fonts.mono->CalcTextSizeA(keySize, FLT_MAX, 0.0f, key).x : 0.0f;
    float arrowWidth = arrow != 0 ? chevron + 10 * s : 0.0f;
    float width = padding + arrowWidth + textWidth + (key ? 12 * s + keyWidth : 0.0f) + padding;
    ImVec2 min(alignRight ? anchor.x - width : anchor.x, anchor.y), max(min.x + width, anchor.y + height);

    // Eased in and out of its hover look, like the lists' rows
    float& hover = *ImGui::GetStateStorage()->GetFloatRef(ImGui::GetID(text), 0.0f);
    ImVec2 mouse = ImGui::GetMousePos();
    bool over = mouse.x >= min.x && mouse.x < max.x && mouse.y >= min.y && mouse.y < max.y && !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopup);
    hover += ((over ? 1.0f : 0.0f) - hover) * std::min(1.0f, std::min(GetFrameTime(), 0.05f) * GLIDE_SPEED);
    if (over) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);

    draw->AddRectFilled(min, max, uiColor(UiColor::Card, 0.55f + 0.45f * hover), height / 2);
    draw->AddRect(min, max, uiColor(UiColor::StaffLine, 1.0f - hover), height / 2, 0, std::max(1.0f, s));
    ImU32 ink = uiColor(UiColor::Ink, 0.62f + 0.38f * hover);
    ImU32 arrowColor = hover > 0.5f ? uiColor(UiColor::Accent) : ink;
    float x = min.x + padding, midY = anchor.y + height / 2;
    float lean = 2 * s * hover; // the chevron leans the way it points
    auto drawChevron = [&](float at, int direction){
        float tip = direction < 0 ? at - lean : at + chevron + lean, back = direction < 0 ? at + chevron - lean : at + lean;
        ImVec2 points[3] = { ImVec2(back, midY - chevron), ImVec2(tip, midY), ImVec2(back, midY + chevron) };
        draw->AddPolyline(points, 3, arrowColor, ImDrawFlags_None, std::max(1.5f, 1.8f * s));
    };
    if (arrow < 0){
        drawChevron(x, -1);
        x += arrowWidth;
    }
    draw->AddText(fonts.bold, textSize, ImVec2(x, midY - textSize / 2 - 1 * s), ink, text);
    x += textWidth;
    if (arrow > 0){
        drawChevron(x + 10 * s, 1);
        x += arrowWidth;
    }
    if (key) draw->AddText(fonts.mono, keySize, ImVec2(x + 12 * s, midY - keySize / 2), uiColor(UiColor::Dim, 0.8f), key);
    return over && ImGui::IsMouseClicked(ImGuiMouseButton_Left);
}

bool menuBackButton(float s){
    // Its text lines up with the title under it; the chevron hangs out to the left
    float left = ImGui::GetIO().DisplaySize.x * 0.07f - 12 * s - 5 * s - 10 * s;
    return menuPill("Back", "Esc", ImVec2(left, 21 * s), false, -1, s);
}

bool menuSwitchRow(const char* label, const char* const* names, int count, int& chosen, float x, float y, float s){
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const UiFonts& fonts = uiFonts();
    draw->AddText(fonts.mono, 13 * s, ImVec2(x, y + 4 * s), uiColor(UiColor::Dim), label);
    float at = x + 100 * s;
    bool changed = false;
    for (int i = 0; i < count; i++){
        bool on = i == chosen;
        ImVec2 size = fonts.bold ? fonts.bold->CalcTextSizeA(20 * s, FLT_MAX, 0.0f, names[i]) : ImVec2(60 * s, 20 * s);
        draw->AddText(fonts.bold, 20 * s, ImVec2(at, y), uiColor(on ? UiColor::Ink : UiColor::Dim), names[i]);
        if (on) draw->AddRectFilled(ImVec2(at, y + size.y + 3 * s), ImVec2(at + size.x, y + size.y + 5 * s), uiColor(UiColor::Accent));
        ImVec2 mouse = ImGui::GetMousePos();
        if (!on && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && mouse.x >= at && mouse.x <= at + size.x && mouse.y >= y && mouse.y <= y + size.y){
            chosen = i;
            changed = true;
        }
        at += size.x + 22 * s;
    }
    return changed;
}
