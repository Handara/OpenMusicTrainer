#include "ui/settingsui.h"

#include "imgui_internal.h"
#include "raylib.h"
#include "ui/menulist.h"

#include <algorithm>
#include <cmath>
#include <cstring>

// At a 720-pixel-tall window; everything scales with it
const float ROW_PADDING = 14.0f;    // above and below a row's text
const float LABEL_SIZE = 17.0f;
const float HINT_SIZE = 14.0f;
const float CONTROL_WIDTH = 300.0f; // the right-hand column
const float COLUMN_GAP = 28.0f;
const float CONTROL_HEIGHT = 34.0f; // dropdowns, buttons, segments
const float TOGGLE_WIDTH = 46.0f;
const float TOGGLE_HEIGHT = 26.0f;
const float KNOB_RADIUS = 9.0f;
const float VALUE_WIDTH = 72.0f;    // a slider's value, right of its track
const float RADIUS = 8.0f;          // the controls' corners
const float TOGGLE_SPEED = 16.0f;   // how fast a toggle's knob slides (per second, of the way)

static float scale(){ return menuScale(); }
static bool disabled(){ return (GImGui->CurrentItemFlags & ImGuiItemFlags_Disabled) != 0; }
static float fade(){ return disabled() ? 0.4f : 1.0f; }

static ImU32 mixColors(UiColor from, UiColor to, float t, float alpha = 1.0f){
    ImVec4 a = uiColorVec(from), b = uiColorVec(to);
    return ImGui::ColorConvertFloat4ToU32(ImVec4(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, alpha));
}

static float textWidth(ImFont* font, float size, const char* text){
    return font ? font->CalcTextSizeA(size, FLT_MAX, 0.0f, text).x : size * 0.5f * std::strlen(text);
}

// Text centered in a box, cut short with "..." if it doesn't fit
static void boxText(ImDrawList* draw, ImFont* font, float size, ImVec2 min, ImVec2 max, ImU32 color, const char* text, bool centered){
    ImVec2 box(max.x - min.x, max.y - min.y);
    float width = textWidth(font, size, text);
    float x = centered ? min.x + (box.x - width) / 2 : min.x;
    ImVec4 clip(min.x, min.y, max.x, max.y);
    draw->AddText(font, size, ImVec2(std::max(min.x, x), min.y + (box.y - size) / 2 - size * 0.05f), color, text, nullptr, 0.0f, &clip);
}

void settingsGroup(const char* title){
    const float s = scale();
    ImGui::Dummy(ImVec2(0, 18 * s));
    ImDrawList* draw = ImGui::GetWindowDrawList();
    ImVec2 at = ImGui::GetCursorScreenPos();
    draw->AddText(uiFonts().mono, 13 * s, at, uiColor(UiColor::Accent), title);
    ImGui::Dummy(ImVec2(0, 13 * s + 4 * s));
}

SettingControl settingRow(const char* label, const char* hint, float controlHeight){
    const float s = scale();
    const UiFonts& fonts = uiFonts();
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec2 start = ImGui::GetCursorScreenPos();
    const float width = ImGui::GetContentRegionAvail().x;
    const float controlWidth = std::min(CONTROL_WIDTH * s, width * 0.45f);
    const float textWidth = width - controlWidth - COLUMN_GAP * s;
    const bool hasHint = hint && *hint;
    ImVec2 labelSize = fonts.bold ? fonts.bold->CalcTextSizeA(LABEL_SIZE * s, FLT_MAX, textWidth, label) : ImVec2(textWidth, LABEL_SIZE * s);
    ImVec2 hintSize = hasHint && fonts.text ? fonts.text->CalcTextSizeA(HINT_SIZE * s, FLT_MAX, textWidth, hint) : ImVec2(0, 0);
    const float textHeight = labelSize.y + (hasHint ? 3 * s + hintSize.y : 0.0f);
    const float height = std::max(textHeight, controlHeight) + 2 * ROW_PADDING * s;
    const float top = start.y + (height - textHeight) / 2;
    const float alpha = fade();
    draw->AddText(fonts.bold, LABEL_SIZE * s, ImVec2(start.x, top), uiColor(UiColor::Ink, alpha), label, nullptr, textWidth);
    if (hasHint) draw->AddText(fonts.text, HINT_SIZE * s, ImVec2(start.x, top + labelSize.y + 3 * s), uiColor(UiColor::Dim, alpha), hint, nullptr, textWidth);
    horizontalLine(draw, start.x, start.x + width, start.y + height - 1.0f, 1.0f, uiColor(UiColor::StaffLine));
    ImGui::ItemSize(ImVec2(width, height));
    float controlTop = start.y + (height - controlHeight) / 2;
    return { ImVec2(start.x + width - controlWidth, controlTop), ImVec2(start.x + width, controlTop + controlHeight) };
}

float settingsControlHeight(){ return CONTROL_HEIGHT * scale(); }

bool settingToggle(const char* label, const char* hint, bool* value){
    const float s = scale();
    SettingControl row = settingRow(label, hint, TOGGLE_HEIGHT * s);
    const ImGuiID id = ImGui::GetID(label);
    const ImVec2 size(TOGGLE_WIDTH * s, TOGGLE_HEIGHT * s);
    const ImRect box(ImVec2(row.max.x - size.x, row.min.y), ImVec2(row.max.x, row.min.y + size.y));
    if (!ImGui::ItemAdd(box, id)) return false;
    bool hovered = false, held = false;
    bool pressed = ImGui::ButtonBehavior(box, id, &hovered, &held);
    if (pressed){
        *value = !*value;
        ImGui::MarkItemEdited(id);
    }
    // The knob slides rather than jumps: where it is between off and on is kept per toggle
    ImGuiStorage* storage = ImGui::GetStateStorage();
    float t = storage->GetFloat(id, *value ? 1.0f : 0.0f);
    t += ((*value ? 1.0f : 0.0f) - t) * std::min(1.0f, ImGui::GetIO().DeltaTime * TOGGLE_SPEED);
    storage->SetFloat(id, t);
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const float alpha = fade(), radius = size.y / 2;
    draw->AddRectFilled(box.Min, box.Max, mixColors(UiColor::StaffLine, UiColor::Accent, t, alpha), radius);
    if (hovered && !disabled()) draw->AddRectFilled(box.Min, box.Max, uiColor(UiColor::Ink, 0.06f), radius);
    float knobX = box.Min.x + radius + t * (size.x - 2 * radius);
    draw->AddCircleFilled(ImVec2(knobX, box.Min.y + radius + 1.0f * s), radius - 3 * s, uiColor(UiColor::Ink, 0.12f * alpha)); // its shadow
    draw->AddCircleFilled(ImVec2(knobX, box.Min.y + radius), radius - 3 * s, uiColor(UiColor::Card, alpha));
    ImGui::RenderNavCursor(box, id);
    return pressed;
}

bool settingSegments(const char* label, const char* hint, int* chosen, const std::vector<const char*>& options){
    const float s = scale();
    SettingControl row = settingRow(label, hint, CONTROL_HEIGHT * s);
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const UiFonts& fonts = uiFonts();
    const float alpha = fade(), count = (float)std::max<size_t>(1, options.size());
    // Each choice as wide as its words, and the room left over shared out evenly: a long one ("Until 100%") isn't
    // cut off beside short ones ("3 times")
    std::vector<float> widths(options.size());
    float words = 0.0f;
    for (size_t i = 0; i < options.size(); i++){
        widths[i] = (fonts.bold ? fonts.bold->CalcTextSizeA(15 * s, FLT_MAX, 0.0f, options[i]).x : 40 * s) + 14 * s;
        words += widths[i];
    }
    const float spare = (row.max.x - row.min.x - words) / count;
    for (float& width : widths) width += spare;
    draw->AddRectFilled(row.min, row.max, uiColor(UiColor::Background, alpha), RADIUS * s);
    float x = row.min.x;
    bool changed = false;
    ImGui::PushID(label);
    for (size_t i = 0; i < options.size(); i++){
        const ImGuiID id = ImGui::GetID((int)i);
        const ImRect box(ImVec2(x, row.min.y), ImVec2(x + widths[i], row.max.y));
        x += widths[i];
        if (!ImGui::ItemAdd(box, id)) continue;
        bool hovered = false, held = false;
        if (ImGui::ButtonBehavior(box, id, &hovered, &held) && *chosen != (int)i){
            *chosen = (int)i;
            changed = true;
            ImGui::MarkItemEdited(id);
        }
        const bool on = *chosen == (int)i;
        ImRect inner(ImVec2(box.Min.x + 3 * s, box.Min.y + 3 * s), ImVec2(box.Max.x - 3 * s, box.Max.y - 3 * s));
        if (on) draw->AddRectFilled(inner.Min, inner.Max, uiColor(UiColor::Accent, alpha), (RADIUS - 2) * s);
        else if (hovered && !disabled()) draw->AddRectFilled(inner.Min, inner.Max, uiColor(UiColor::Ink, 0.06f), (RADIUS - 2) * s);
        boxText(draw, fonts.bold, 15 * s, inner.Min, inner.Max, on ? uiColor(UiColor::Card, alpha) : uiColor(hovered ? UiColor::Ink : UiColor::Dim, alpha),
                options[i], true);
        ImGui::RenderNavCursor(box, id);
    }
    ImGui::PopID();
    return changed;
}

// A slider's track and knob in `row`, the value's text right of it; `t` is where the value is from 0 to 1. Returns
// where the mouse or the keyboard moved it, -1 for nowhere.
static float sliderAt(const char* label, SettingControl row, float t, const char* text, float step){
    const float s = scale();
    const ImGuiID id = ImGui::GetID(label);
    const float trackLeft = row.min.x + KNOB_RADIUS * s, trackRight = row.max.x - VALUE_WIDTH * s - KNOB_RADIUS * s;
    const float middle = (row.min.y + row.max.y) / 2;
    const ImRect box(ImVec2(row.min.x, row.min.y), ImVec2(trackRight + KNOB_RADIUS * s, row.max.y));
    if (!ImGui::ItemAdd(box, id)) return -1.0f;
    bool hovered = false, held = false;
    ImGui::ButtonBehavior(box, id, &hovered, &held, ImGuiButtonFlags_PressedOnClick);
    float moved = -1.0f;
    if (held) moved = std::clamp((ImGui::GetIO().MousePos.x - trackLeft) / (trackRight - trackLeft), 0.0f, 1.0f);
    // Focused with the keyboard: Left and Right move it (Shift: ten times as far). The slider takes those keys for
    // itself, or the keyboard navigation would use them to go looking for another control.
    if (GImGui->NavId == id && !disabled()){
        ImGui::SetKeyOwner(ImGuiKey_LeftArrow, id);
        ImGui::SetKeyOwner(ImGuiKey_RightArrow, id);
        float by = step * (ImGui::GetIO().KeyShift ? 10.0f : 1.0f);
        if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow, ImGuiInputFlags_Repeat, id)) moved = std::clamp(t - by, 0.0f, 1.0f);
        if (ImGui::IsKeyPressed(ImGuiKey_RightArrow, ImGuiInputFlags_Repeat, id)) moved = std::clamp(t + by, 0.0f, 1.0f);
    }
    const float shown = moved >= 0.0f ? moved : t;
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const float alpha = fade(), knobX = trackLeft + shown * (trackRight - trackLeft);
    draw->AddRectFilled(ImVec2(trackLeft, middle - 2 * s), ImVec2(trackRight, middle + 2 * s), uiColor(UiColor::Dim, 0.3f * alpha), 2 * s); // seen in dark too
    draw->AddRectFilled(ImVec2(trackLeft, middle - 2 * s), ImVec2(knobX, middle + 2 * s), uiColor(UiColor::Accent, alpha), 2 * s);
    float knob = KNOB_RADIUS * s * (held ? 1.15f : hovered ? 1.08f : 1.0f);
    draw->AddCircleFilled(ImVec2(knobX, middle + 1.0f * s), knob, uiColor(UiColor::Ink, 0.12f * alpha)); // its shadow
    draw->AddCircleFilled(ImVec2(knobX, middle), knob, uiColor(UiColor::Card, alpha));
    draw->AddCircle(ImVec2(knobX, middle), knob, uiColor(UiColor::Accent, alpha), 0, 2.0f * s);
    const UiFonts& fonts = uiFonts();
    float valueWidth = textWidth(fonts.mono, 14 * s, text);
    draw->AddText(fonts.mono, 14 * s, ImVec2(row.max.x - valueWidth, middle - 8 * s), uiColor(UiColor::Ink, alpha), text);
    ImGui::RenderNavCursor(box, id);
    return moved;
}

bool settingSlider(const char* label, const char* hint, float* value, float min, float max, const char* format){
    SettingControl row = settingRow(label, hint, TOGGLE_HEIGHT * scale());
    float t = max > min ? (*value - min) / (max - min) : 0.0f;
    float moved = sliderAt(label, row, t, TextFormat(format, *value), 0.01f);
    if (moved < 0.0f) return false;
    float next = min + moved * (max - min);
    if (next == *value) return false;
    *value = next;
    ImGui::MarkItemEdited(ImGui::GetID(label));
    return true;
}

bool settingSliderInt(const char* label, const char* hint, int* value, int min, int max, const char* format){
    SettingControl row = settingRow(label, hint, TOGGLE_HEIGHT * scale());
    float t = max > min ? (float)(*value - min) / (max - min) : 0.0f;
    float moved = sliderAt(label, row, t, TextFormat(format, *value), 1.0f / std::max(1, max - min)); // a key moves it by one
    if (moved < 0.0f) return false;
    int next = min + (int)std::lround(moved * (max - min));
    if (next == *value) return false;
    *value = next;
    ImGui::MarkItemEdited(ImGui::GetID(label));
    return true;
}

bool settingsDropdownAt(const char* idText, ImVec2 min, ImVec2 max, int* chosen, const std::vector<std::string>& options,
                        const std::function<void()>& onOpen){
    const float s = scale();
    const ImGuiID id = ImGui::GetID(idText);
    const ImRect box(min, max);
    if (!ImGui::ItemAdd(box, id)) return false;
    bool hovered = false, held = false;
    const std::string popup = std::string("##dropdown") + idText;
    if (ImGui::ButtonBehavior(box, id, &hovered, &held)){
        if (onOpen) onOpen();
        ImGui::OpenPopup(popup.c_str());
    }
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const UiFonts& fonts = uiFonts();
    const float alpha = fade();
    const bool open = ImGui::IsPopupOpen(popup.c_str());
    draw->AddRectFilled(min, max, uiColor(UiColor::Background, alpha), RADIUS * s);
    draw->AddRect(min, max, uiColor(open || (hovered && !disabled()) ? UiColor::Dim : UiColor::StaffLine, alpha), RADIUS * s, 0, 1.0f);
    const char* current = *chosen >= 0 && *chosen < (int)options.size() ? options[*chosen].c_str() : "";
    boxText(draw, fonts.bold, 15 * s, ImVec2(min.x + 12 * s, min.y), ImVec2(max.x - 30 * s, max.y), uiColor(UiColor::Ink, alpha), current, false);
    // A chevron pointing down (up while open)
    ImVec2 c(max.x - 17 * s, (min.y + max.y) / 2);
    float w = 4.5f * s, h = open ? -2.5f * s : 2.5f * s;
    draw->AddLine(ImVec2(c.x - w, c.y - h), ImVec2(c.x, c.y + h), uiColor(UiColor::Dim, alpha), 1.8f * s);
    draw->AddLine(ImVec2(c.x, c.y + h), ImVec2(c.x + w, c.y - h), uiColor(UiColor::Dim, alpha), 1.8f * s);
    ImGui::RenderNavCursor(box, id);

    bool changed = false;
    ImGui::SetNextWindowPos(ImVec2(min.x, max.y + 4 * s));
    ImGui::SetNextWindowSizeConstraints(ImVec2(max.x - min.x, 0), ImVec2(std::max(max.x - min.x, 420 * s), 360 * s));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6 * s, 6 * s));
    ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, 10 * s);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 2 * s));
    if (ImGui::BeginPopup(popup.c_str())){
        ImDrawList* list = ImGui::GetWindowDrawList();
        const float rowHeight = 32 * s, width = std::max(max.x - min.x - 12 * s, ImGui::GetContentRegionAvail().x);
        for (int i = 0; i < (int)options.size(); i++){
            ImGui::PushID(i);
            ImVec2 at = ImGui::GetCursorScreenPos();
            bool picked = ImGui::InvisibleButton("option", ImVec2(width, rowHeight));
            bool over = ImGui::IsItemHovered() || ImGui::IsItemFocused();
            bool isChosen = i == *chosen;
            if (over) list->AddRectFilled(at, ImVec2(at.x + width, at.y + rowHeight), uiColor(UiColor::Ink, 0.06f), 6 * s);
            if (isChosen) list->AddRectFilled(ImVec2(at.x + 4 * s, at.y + 8 * s), ImVec2(at.x + 7 * s, at.y + rowHeight - 8 * s), uiColor(UiColor::Accent), 1.5f * s);
            boxText(list, isChosen ? fonts.bold : fonts.text, 15 * s, ImVec2(at.x + 16 * s, at.y), ImVec2(at.x + width - 8 * s, at.y + rowHeight),
                    uiColor(UiColor::Ink, isChosen ? 1.0f : 0.85f), options[i].c_str(), false);
            if (isChosen && ImGui::IsWindowAppearing()) ImGui::SetScrollHereY(0.5f);
            if (picked){
                changed = i != *chosen;
                *chosen = i;
                ImGui::CloseCurrentPopup();
            }
            ImGui::PopID();
        }
        ImGui::EndPopup();
    }
    ImGui::PopStyleVar(3);
    if (changed) ImGui::MarkItemEdited(id);
    return changed;
}

bool settingDropdown(const char* label, const char* hint, int* chosen, const std::vector<std::string>& options,
                     const std::function<void()>& onOpen){
    SettingControl row = settingRow(label, hint, CONTROL_HEIGHT * scale());
    return settingsDropdownAt(label, row.min, row.max, chosen, options, onOpen);
}

bool settingsButtonAt(const char* idText, ImVec2 min, ImVec2 max, const char* text){
    const float s = scale();
    const ImGuiID id = ImGui::GetID(idText);
    const ImRect box(min, max);
    if (!ImGui::ItemAdd(box, id)) return false;
    bool hovered = false, held = false;
    bool pressed = ImGui::ButtonBehavior(box, id, &hovered, &held);
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const float alpha = fade();
    bool live = !disabled();
    draw->AddRectFilled(min, max, held && live ? uiColor(UiColor::Accent, 0.9f) : hovered && live ? uiColor(UiColor::Accent, 0.12f) : uiColor(UiColor::Card, alpha), RADIUS * s);
    draw->AddRect(min, max, uiColor(hovered && live ? UiColor::Accent : UiColor::StaffLine, alpha), RADIUS * s, 0, 1.0f);
    boxText(draw, uiFonts().bold, 15 * s, min, max, held && live ? uiColor(UiColor::Card) : uiColor(UiColor::Ink, alpha), text, true);
    ImGui::RenderNavCursor(box, id);
    return pressed;
}

int settingButtons(const char* label, const char* hint, const std::vector<const char*>& buttons){
    const float s = scale();
    SettingControl row = settingRow(label, hint, CONTROL_HEIGHT * s);
    const UiFonts& fonts = uiFonts();
    // Right-aligned, each as wide as its text needs
    float x = row.max.x;
    int pressed = -1;
    ImGui::PushID(label);
    for (int i = (int)buttons.size() - 1; i >= 0; i--){
        float width = textWidth(fonts.bold, 15 * s, buttons[i]) + 32 * s;
        if (settingsButtonAt(buttons[i], ImVec2(x - width, row.min.y), ImVec2(x, row.max.y), buttons[i])) pressed = i;
        x -= width + 8 * s;
    }
    ImGui::PopID();
    return pressed;
}

bool settingButton(const char* label, const char* hint, const char* button){
    return settingButtons(label, hint, { button }) == 0;
}

void settingInfo(const char* label, const char* hint, const char* value, UiColor color){
    const float s = scale();
    const UiFonts& fonts = uiFonts();
    SettingControl row = settingRow(label, hint, CONTROL_HEIGHT * s);
    const float width = row.max.x - row.min.x;
    ImVec2 size = fonts.text ? fonts.text->CalcTextSizeA(15 * s, FLT_MAX, width, value) : ImVec2(width, 15 * s);
    // Right-aligned when it fits on a line; wrapped from the left of the column when it doesn't
    float x = size.x < width ? row.max.x - size.x : row.min.x;
    ImGui::GetWindowDrawList()->AddText(fonts.text, 15 * s, ImVec2(x, (row.min.y + row.max.y - size.y) / 2), uiColor(color, fade()), value, nullptr, width);
}

void settingNote(const char* text, UiColor color){
    const float s = scale();
    const UiFonts& fonts = uiFonts();
    const float width = ImGui::GetContentRegionAvail().x;
    ImVec2 at = ImGui::GetCursorScreenPos();
    ImVec2 size = fonts.text ? fonts.text->CalcTextSizeA(14 * s, FLT_MAX, width, text) : ImVec2(width, 14 * s);
    ImGui::GetWindowDrawList()->AddText(fonts.text, 14 * s, ImVec2(at.x, at.y + 10 * s), uiColor(color, fade()), text, nullptr, width);
    ImGui::Dummy(ImVec2(width, size.y + 20 * s));
}
