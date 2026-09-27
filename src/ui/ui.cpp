#include "ui/ui.h"

#include "raylib.h"
#include "rlImGui.h"
#include "ui/theme.h"

#include <filesystem>

const float TITLE_FONT_SIZE = 56.0f;
const float MENU_BUTTON_WIDTH = 420.0f;
const float MENU_BUTTON_HEIGHT = 56.0f;

void initUi(const std::string& resourcesDir, bool darkTheme){
    rlImGuiSetup(true);
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr; // don't write imgui.ini: menu layout is fixed in code
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard; // arrows + Enter work in menus
    initTheme(resourcesDir, darkTheme ? ThemeMode::Dark : ThemeMode::Light); // fonts, colors and the wordmark (ui/theme)
}

void closeUi(){
    closeTheme();
    rlImGuiShutdown();
}

void beginUiFrame(){
    rlImGuiBegin();
}

void endUiFrame(){
    rlImGuiEnd();
}

void drawMenuBackground(){
    ClearBackground(themeColor(UiColor::Background));
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
    ImGui::PushFont(uiFonts().heavy, TITLE_FONT_SIZE);
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
    ImGui::PushStyleColor(ImGuiCol_Text, uiColorVec(UiColor::Bad));
    ImGui::PushTextWrapPos(left + MENU_BUTTON_WIDTH);
    ImGui::TextUnformatted(text.c_str());
    ImGui::PopTextWrapPos();
    ImGui::PopStyleColor();
}
