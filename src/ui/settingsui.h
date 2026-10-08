#pragma once

#include "imgui.h"
#include "ui/theme.h"

#include <functional>
#include <string>
#include <vector>

// The settings' look: each setting a row, its name and a line of explanation on the left and its control on the
// right, hairlines between rows, rows gathered under small headers. The controls are lahn's own (toggle switches,
// segmented choices, slim sliders, dropdowns, buttons) in the theme's colors, and all reachable with the keyboard: Up
// and Down move between them, Left and Right move a slider, Enter or Space presses. Drawn inside a scrolling panel.
// A control inside ImGui::BeginDisabled is drawn faded and can't be used. Each returns true when it changed a value.

void settingsGroup(const char* title); // the small header over a group of rows

bool settingToggle(const char* label, const char* hint, bool* value);
bool settingSegments(const char* label, const char* hint, int* chosen, const std::vector<const char*>& options);
bool settingSlider(const char* label, const char* hint, float* value, float min, float max, const char* format);
bool settingSliderInt(const char* label, const char* hint, int* value, int min, int max, const char* format);
// One of a list, shown closed as the one chosen. `onOpen` runs as it opens (to look for devices again), may be empty.
bool settingDropdown(const char* label, const char* hint, int* chosen, const std::vector<std::string>& options,
                     const std::function<void()>& onOpen = {});
// Buttons side by side on the right: the index of the one pressed, -1 for none
int settingButtons(const char* label, const char* hint, const std::vector<const char*>& buttons);
bool settingButton(const char* label, const char* hint, const char* button);
// A line of text to type (at most `longest` characters); true when it's changed
bool settingText(const char* label, const char* hint, std::string* value, int longest, const char* placeholder = "");
// A setting shown, not changed here: its value on the right
void settingInfo(const char* label, const char* hint, const char* value, UiColor color = UiColor::Dim);
// A line across the panel, with no control: a status, a warning
void settingNote(const char* text, UiColor color = UiColor::Dim);

// A row whose control the caller draws, `controlHeight` tall: where it goes
struct SettingControl {
    ImVec2 min, max;
};
SettingControl settingRow(const char* label, const char* hint, float controlHeight);
// Controls to put in such a row, or anywhere: the same dropdown and button as above, in a given rectangle
bool settingsDropdownAt(const char* id, ImVec2 min, ImVec2 max, int* chosen, const std::vector<std::string>& options,
                        const std::function<void()>& onOpen = {});
bool settingsButtonAt(const char* id, ImVec2 min, ImVec2 max, const char* text);
float settingsControlHeight(); // a dropdown's or a button's height at this window size
