#include "ui/helpoverlay.h"

#include "imgui.h"
#include "ui/theme.h"

#include <cfloat>

namespace {
struct HelpState {
    bool open = false;
};
struct HelpLine {
    const char* keys;
    const char* what;
};
struct HelpSection {
    const char* title;
    std::initializer_list<HelpLine> lines;
};
}
static HelpState help;

void toggleHelp(){ help.open = !help.open; }
bool helpOpen(){ return help.open; }
void closeHelp(){ help.open = false; }

void drawHelpOverlay(float s){
    if (!help.open) return;
    ImDrawList* draw = ImGui::GetForegroundDrawList();
    const UiFonts& fonts = uiFonts();
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    draw->AddRectFilled(ImVec2(0, 0), display, uiColor(UiColor::Background, 0.97f));
    const float left = display.x * 0.07f, top = display.y * 0.08f;
    draw->AddText(fonts.heavy, 30 * s, ImVec2(left, top), uiColor(UiColor::Ink), "Controls");
    draw->AddText(fonts.text, 15 * s, ImVec2(left, top + 40 * s), uiColor(UiColor::Dim), "F1 or Esc to close");
    static const HelpSection columns[2][4] = {
        {
            { "EVERYWHERE", { { "Esc", "back (the mouse's back button too)" }, { "F1", "these controls" },
                              { "F2", "hear your instrument, or not" }, { "Up / Down, Enter", "choose in a list" } } },
            { "THE MENUS, BY PLAYING", { { "low E", "back" }, { "A / D", "up / down" }, { "G", "choose" },
                                          { "B / high E", "left / right (bass: the G string's 4th and 5th frets)" },
                                          { "a note shown", "beside a row: straight to it" } } },
            { "LEARN", { { "Left / Right", "courses, drills, games" }, { "I", "guitar, bass or piano" }, { "Tab", "edit lessons" } } },
            { "PROFILE", { { "Left / Right", "the daily goal" } } },
        },
        {
            { "TIMED DRILLS", { { "Space or G string", "start, stop" }, { "Up / Down", "the tempo, before starting" },
                                { "M", "the band, or the metronome alone" }, { "A to G", "a note from the keyboard (Shift sharp, Ctrl flat)" },
                                { "after a pass", "Space again, C the challenge, N the next drill" } } },
            { "PLAY THIS NOTE", { { "A to G", "name the note" }, { "Space", "hear it again (by ear)" } } },
            { "SONGS", { { "Esc", "pause: retry, practise, tune, metronome" }, { "1 to 8", "the strings, from the keyboard (1 the lowest)" },
                         { "F9", "record a check of what's heard" } } },
            { "PRACTICE", { { "Space", "listen to the section" }, { "Left / Right", "move the section (Shift: its length)" },
                            { "M", "in the pause menu: the metronome" } } },
        },
    };
    for (int c = 0; c < 2; c++){
        const float x = left + c * display.x * 0.44f;
        float y = top + 86 * s;
        for (const HelpSection& section : columns[c]){
            draw->AddText(fonts.mono, 12 * s, ImVec2(x, y), uiColor(UiColor::Accent), section.title);
            y += 22 * s;
            for (const HelpLine& line : section.lines){
                draw->AddText(fonts.bold, 15 * s, ImVec2(x, y), uiColor(UiColor::Ink), line.keys);
                draw->AddText(fonts.text, 15 * s, ImVec2(x + 150 * s, y), uiColor(UiColor::Dim), line.what);
                y += 22 * s;
            }
            y += 14 * s;
        }
    }
}
