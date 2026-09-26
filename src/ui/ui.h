#pragma once

#include "imgui.h"

#include <string>

// Shared look and layout for ImGui screens. Each menu is one invisible full-screen window
// with its content centered horizontally.

void initUi(const std::string& fontPath); // after InitWindow: sets up ImGui, font, colors, keyboard navigation
void closeUi();
void beginUiFrame(); // ImGui widgets can be used between these two, inside BeginDrawing/EndDrawing
void endUiFrame();

void drawMenuBackground();
void openFolder(const std::string& path); // in Explorer / Finder / the Linux file manager
void beginMenu(const char* id); // pair with ImGui::End()
void menuTitle(const char* text);
bool menuButton(const char* label);
// Call right before a menu's main button: when the menu appears, that button is selected, so Enter
// works immediately (ImGui's own default focus only kicks in after the first arrow key)
void focusNextWhenMenuAppears();
void centeredText(const char* text);
void centeredColoredText(const char* text, ImU32 color);
void centeredErrorText(const std::string& text);
