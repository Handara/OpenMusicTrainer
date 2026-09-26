#include "ui/ui.h"

#include "raylib.h"
#include "rlImGui.h"

#include <filesystem>

const float UI_FONT_SIZE = 26.0f;
const float TITLE_FONT_SIZE = 56.0f;
const float MENU_BUTTON_WIDTH = 420.0f;
const float MENU_BUTTON_HEIGHT = 56.0f;
const ImVec4 ERROR_TEXT_COLOR = { 1.0f, 0.45f, 0.4f, 1.0f };

const Color MENU_BG_TOP = { 28, 18, 14, 255 };
const Color MENU_BG_BOTTOM = { 70, 42, 28, 255 };

void initUi(const std::string& fontPath){
    rlImGuiSetup(true);
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr; // don't write imgui.ini: menu layout is fixed in code
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard; // arrows + Enter work in menus

    if (FileExists(fontPath.c_str())){
        io.FontDefault = io.Fonts->AddFontFromFileTTF(fontPath.c_str(), UI_FONT_SIZE);
    } else {
        TraceLog(LOG_WARNING, "UI font not found, using ImGui's default: %s", fontPath.c_str());
    }

    ImGuiStyle& style = ImGui::GetStyle();
    style.FontSizeBase = UI_FONT_SIZE;
    style.FrameRounding = 8.0f;
    style.ItemSpacing = ImVec2(12, 14);
    style.Colors[ImGuiCol_Button] = ImVec4(0.45f, 0.28f, 0.18f, 0.85f);
    style.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.62f, 0.40f, 0.24f, 1.0f);
    style.Colors[ImGuiCol_ButtonActive] = ImVec4(0.80f, 0.58f, 0.20f, 1.0f);
    style.Colors[ImGuiCol_NavCursor] = ImVec4(1.0f, 0.80f, 0.30f, 1.0f);

    // Every other widget in the same warm palette: fields, sliders, tabs, list highlights
    const ImVec4 fieldColor(0.10f, 0.06f, 0.045f, 1.0f); // darker than the background, so fields read as slots
    const ImVec4 hoverColor(0.62f, 0.40f, 0.24f, 1.0f);
    const ImVec4 activeColor(0.80f, 0.58f, 0.20f, 1.0f);
    const ImVec4 accentColor(0.95f, 0.72f, 0.30f, 1.0f);
    style.Colors[ImGuiCol_FrameBg] = fieldColor;
    style.Colors[ImGuiCol_FrameBgHovered] = ImVec4(0.16f, 0.10f, 0.07f, 1.0f);
    style.Colors[ImGuiCol_FrameBgActive] = ImVec4(0.22f, 0.14f, 0.10f, 1.0f);
    style.Colors[ImGuiCol_SliderGrab] = accentColor;
    style.Colors[ImGuiCol_SliderGrabActive] = activeColor;
    style.Colors[ImGuiCol_CheckMark] = accentColor;
    style.Colors[ImGuiCol_Header] = ImVec4(0.45f, 0.28f, 0.18f, 0.85f);
    style.Colors[ImGuiCol_HeaderHovered] = hoverColor;
    style.Colors[ImGuiCol_HeaderActive] = activeColor;
    style.Colors[ImGuiCol_Tab] = ImVec4(0.30f, 0.19f, 0.13f, 1.0f);
    style.Colors[ImGuiCol_TabHovered] = hoverColor;
    style.Colors[ImGuiCol_TabSelected] = ImVec4(0.55f, 0.35f, 0.21f, 1.0f);
    style.Colors[ImGuiCol_TabSelectedOverline] = accentColor;
    style.Colors[ImGuiCol_PopupBg] = ImVec4(0.16f, 0.10f, 0.08f, 0.97f);
    style.Colors[ImGuiCol_SeparatorHovered] = hoverColor;
    style.Colors[ImGuiCol_SeparatorActive] = activeColor;
}

void closeUi(){
    rlImGuiShutdown();
}

void beginUiFrame(){
    rlImGuiBegin();
}

void endUiFrame(){
    rlImGuiEnd();
}

void drawMenuBackground(){
    DrawRectangleGradientV(0, 0, GetScreenWidth(), GetScreenHeight(), MENU_BG_TOP, MENU_BG_BOTTOM);
}

void openFolder(const std::string& path){
    // raylib's OpenURL hands the path to the system (Explorer, Finder, xdg-open), which opens folders too.
    // make_preferred() gives Explorer the backslashes it expects on Windows.
    std::string native = std::filesystem::path(path).make_preferred().string();
    OpenURL(native.c_str());
}

void beginMenu(const char* id){
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove
                           | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoSavedSettings;
    ImGui::Begin(id, nullptr, flags);
    ImGui::Dummy(ImVec2(0, 60)); // top margin
}

void centeredText(const char* text){
    ImGui::SetCursorPosX((ImGui::GetWindowWidth() - ImGui::CalcTextSize(text).x) / 2);
    ImGui::TextUnformatted(text);
}

void centeredColoredText(const char* text, ImU32 color){
    ImGui::PushStyleColor(ImGuiCol_Text, color);
    centeredText(text);
    ImGui::PopStyleColor();
}

void menuTitle(const char* text){
    ImGui::PushFont(nullptr, TITLE_FONT_SIZE); // same font, bigger size
    centeredText(text);
    ImGui::PopFont();
    ImGui::Dummy(ImVec2(0, 30));
}

void focusNextWhenMenuAppears(){
    if (!ImGui::IsWindowAppearing()) return;
    ImGui::SetKeyboardFocusHere();
    // ImGui hides the keyboard cursor until an arrow key is pressed, and a hidden cursor ignores Enter/Space.
    // Showing it right away makes Enter work at once; ImGui hides it again as soon as the mouse is used.
    ImGui::SetNavCursorVisible(true);
}

bool menuButton(const char* label){
    ImGui::SetCursorPosX((ImGui::GetWindowWidth() - MENU_BUTTON_WIDTH) / 2);
    return ImGui::Button(label, ImVec2(MENU_BUTTON_WIDTH, MENU_BUTTON_HEIGHT));
}

void centeredErrorText(const std::string& text){
    // Errors can be long (they include file paths), so wrap them to the button column's width
    float left = (ImGui::GetWindowWidth() - MENU_BUTTON_WIDTH) / 2;
    ImGui::SetCursorPosX(left);
    ImGui::PushStyleColor(ImGuiCol_Text, ERROR_TEXT_COLOR);
    ImGui::PushTextWrapPos(left + MENU_BUTTON_WIDTH);
    ImGui::TextUnformatted(text.c_str());
    ImGui::PopTextWrapPos();
    ImGui::PopStyleColor();
}
