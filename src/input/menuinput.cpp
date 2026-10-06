#include "input/menuinput.h"

#include "audio/audio.h"
#include "core/menunotes.h"
#include "core/music.h"
#include "imgui.h"
#include "input/noteinput.h"
#include "raylib.h"
#include "ui/theme.h"

#include <algorithm>
#include <cfloat>

const double ACTION_GAP_S = 0.25;   // one action at a time: a strum, a string still ringing, aren't several presses
const double RETRY_AFTER_S = 3.0;   // the input wouldn't open: try again after this (opening a device is slow)

namespace {
struct MenuInputState {
    bool listening = false;
    bool bass = false;
    int noteGeneration = -1, captureGeneration = -1; // what was started, to know it's still ours
    double failedAt = -100.0;
    double lastActionAt = -100.0;
    ImGuiKey release = ImGuiKey_None; // a key pressed last frame, let go now
    bool back = false;
    int item = -1;
    int heard = -1;
    double heardAt = -100.0;
    bool wanted = false;   // a menu screen this frame, with the instrument connected
    std::string error;     // why it can't listen, for the legend to say
};
}
static MenuInputState menuInput;

static void stopListening(){
    if (!menuInput.listening) return;
    menuInput.listening = false;
    if (noteInputGeneration() != menuInput.noteGeneration) return;        // another screen's now: leave it be
    if (captureGeneration() == menuInput.captureGeneration) stopNoteInput();
    else releaseNoteInput();                                             // the capture is another screen's
}

void updateMenuInput(bool menuScreen, const std::string& problem, const Settings& settings, InputRole instrument){
    const bool listen = menuScreen && problem.empty();
    ImGuiIO& io = ImGui::GetIO();
    if (menuInput.release != ImGuiKey_None){
        io.AddKeyEvent(menuInput.release, false);
        menuInput.release = ImGuiKey_None;
    }
    menuInput.back = false;
    menuInput.item = -1;
    menuInput.wanted = menuScreen;
    if (menuScreen && !problem.empty()) menuInput.error = problem;
    const bool bass = instrument == InputRole::Bass;
    if (!listen || (menuInput.listening && bass != menuInput.bass)){
        stopListening();
        if (!listen) return;
    }
    // Still ours: the note input this listener started, on the capture as it started it. Anyone opening the capture
    // since (the check of which instruments are connected opens and closes it) leaves it unread or closed under us:
    // then it's started again.
    const bool noteInputOurs = menuInput.listening && noteInputActive() && noteInputGeneration() == menuInput.noteGeneration;
    const bool ours = noteInputOurs && captureGeneration() == menuInput.captureGeneration;
    if (!ours){
        if (noteInputOurs) releaseNoteInput(); // ours, on a capture someone else opened since: start over on it
        menuInput.listening = false;
        if (GetTime() - menuInput.failedAt < RETRY_AFTER_S) return;
        std::string error;
        const int channel = bass ? settings.bassChannel : settings.guitarChannel;
        const float lowest = midiToFrequency(bass ? 28.0f : 40.0f) * 0.9f;
        if (!startNoteInput(settings.inputDevice, lowest, error, channel)){
            menuInput.failedAt = GetTime();
            menuInput.error = error;
            TraceLog(LOG_WARNING, "Menus by the instrument: %s", error.c_str());
            return;
        }
        menuInput.error.clear();
        menuInput.listening = true;
        menuInput.bass = bass;
        menuInput.noteGeneration = noteInputGeneration();
        menuInput.captureGeneration = captureGeneration();
    }
    const MenuNoteMap& map = menuNoteMap(bass);
    for (const PlayedNote& note : updateNoteInput()){
        if (note.legato || io.WantTextInput) continue;      // a plucked note, and not while typing
        if (GetTime() - menuInput.lastActionAt < ACTION_GAP_S) continue;
        int item = -1;
        const MenuNoteAction action = menuNoteAction(map, note.pitch, item);
        if (action == MenuNoteAction::None) continue;
        menuInput.lastActionAt = GetTime();
        ImGuiKey key = ImGuiKey_None;
        switch (action){
            case MenuNoteAction::Back: menuInput.back = true; menuInput.heard = 0; break;
            case MenuNoteAction::Up: key = ImGuiKey_UpArrow; menuInput.heard = 1; break;
            case MenuNoteAction::Down: key = ImGuiKey_DownArrow; menuInput.heard = 2; break;
            case MenuNoteAction::Choose: key = ImGuiKey_Enter; menuInput.heard = 3; break;
            case MenuNoteAction::Left: key = ImGuiKey_LeftArrow; menuInput.heard = 4; break;
            case MenuNoteAction::Right: key = ImGuiKey_RightArrow; menuInput.heard = 5; break;
            case MenuNoteAction::Item: menuInput.item = item; break;
            case MenuNoteAction::None: break;
        }
        menuInput.heardAt = GetTime();
        if (key != ImGuiKey_None){
            io.AddKeyEvent(key, true);
            menuInput.release = key;
        }
        break; // one a frame
    }
}

bool menuInputActive(){ return menuInput.listening; }
bool menuInputBass(){ return menuInput.bass; }
bool menuInputBack(){ return menuInput.back; }
int menuInputItem(){ return menuInput.item; }

int menuInputHeard(double& at){
    at = menuInput.heardAt;
    return menuInput.heard;
}

void drawMenuInputLegend(float s){
    ImDrawList* draw = ImGui::GetForegroundDrawList();
    const UiFonts& fonts = uiFonts();
    if (!menuInput.listening){
        // Wanted but not listening: say why, so it isn't just silently gone
        if (menuInput.wanted && !menuInput.error.empty()){
            const ImVec2 display = ImGui::GetIO().DisplaySize;
            const std::string text = "Playing to move: " + menuInput.error;
            const ImVec2 size = fonts.mono->CalcTextSizeA(11 * s, FLT_MAX, 0.0f, text.c_str());
            draw->AddText(fonts.mono, 11 * s, ImVec2(display.x * 0.93f - size.x, display.y - 70 * s), uiColor(UiColor::Bad), text.c_str());
        }
        return;
    }
    const MenuNoteMap& map = menuNoteMap(menuInput.bass);
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    const int strings = (int)map.tuning.size(), frets = menuInput.bass ? 5 : 0;
    const float spacing = 13 * s, fretWidth = 18 * s, board = 26 * s + fretWidth * frets, labels = 56 * s;
    const float right = display.x * 0.93f, bottom = display.y - 60 * s - (menuInput.bass ? 16 * s : 0.0f);
    const float left = right - (24 * s + board + labels), top = bottom - spacing * (strings - 1), nutX = left + 24 * s;
    draw->AddText(fonts.mono, 11 * s, ImVec2(left, top - 24 * s), uiColor(UiColor::Dim), "PLAY TO MOVE");
    // The neck: its strings, the lowest at the bottom, a few frets on a bass
    for (int string = 0; string < strings; string++){
        const float y = bottom - string * spacing;
        draw->AddLine(ImVec2(nutX, y), ImVec2(nutX + board, y), uiColor(UiColor::Dim, 0.6f), 1.0f * s);
        draw->AddText(fonts.mono, 10 * s, ImVec2(left, y - 6 * s), uiColor(UiColor::Dim), pitchClassName(map.tuning[string]));
    }
    for (int fret = 1; fret <= frets; fret++)
        draw->AddLine(ImVec2(nutX + fret * fretWidth, top - 2 * s), ImVec2(nutX + fret * fretWidth, bottom + 2 * s), uiColor(UiColor::Dim, 0.4f), 1.0f * s);
    draw->AddLine(ImVec2(nutX, top - 3 * s), ImVec2(nutX, bottom + 3 * s), uiColor(UiColor::Ink, 0.7f), 2 * s);
    // Each control where it's played, the one just heard lit; an open string's word at its end
    struct Control { int pitch; const char* word; };
    const Control controls[6] = { { map.back, "back" }, { map.up, "up" }, { map.down, "down" }, { map.choose, "choose" },
                                  { map.left, "left" }, { map.right, "right" } };
    double heardAt;
    const int heard = menuInputHeard(heardAt);
    const float flash = std::max(0.0f, 1.0f - (float)(GetTime() - heardAt) / 0.6f);
    std::string fretted;
    for (int i = 0; i < 6; i++){
        int string = -1, fret = 0;
        for (int j = 0; j < strings; j++){
            const int f = controls[i].pitch - map.tuning[j];
            if (f >= 0 && f <= frets && (string < 0 || f < fret)){ string = j; fret = f; }
        }
        if (string < 0) continue;
        const bool lit = heard == i && flash > 0.0f;
        const float y = bottom - string * spacing, x = fret == 0 ? nutX - 6 * s : nutX + (fret - 0.5f) * fretWidth;
        draw->AddCircleFilled(ImVec2(x, y), 3.5f * s + (lit ? 2.5f * s * flash : 0.0f), uiColor(lit ? UiColor::Accent : UiColor::Ink, lit ? 1.0f : 0.85f), 12);
        if (fret == 0) draw->AddText(fonts.mono, 10 * s, ImVec2(nutX + board + 8 * s, y - 6 * s), uiColor(lit ? UiColor::Accent : UiColor::Dim), controls[i].word);
        else fretted += std::string(fretted.empty() ? "" : ", ") + controls[i].word + " fret " + std::to_string(fret);
    }
    if (!fretted.empty()) // a bass's left and right, fretted on its G string
        draw->AddText(fonts.mono, 10 * s, ImVec2(left, bottom + 8 * s), uiColor(UiColor::Dim), ("G string: " + fretted).c_str());
}
