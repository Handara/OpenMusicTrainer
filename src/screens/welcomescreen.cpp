#include "screens/welcomescreen.h"

#include "core/inputs.h"
#include "core/music.h"
#include "imgui.h"
#include "input/keysinput.h"
#include "input/noteinput.h"
#include "raylib.h"
#include "ui/menulist.h"
#include "ui/theme.h"
#include "ui/ui.h"

#include <algorithm>
#include <cfloat>
#include <string>

namespace {
enum class WelcomeStep { Instrument, PlugIn, Goal, Ready };
struct WelcomeState {
    WelcomeStep step = WelcomeStep::Instrument;
    int instrument = 0;        // guitar, bass, piano
    MenuList lists[4];
    bool listening = false;    // the instrument's input, or the piano's keys, while plugging in
    std::string inputError;
    int heardPitch = -1;       // the last note heard
    double heardAt = -100.0;
    float level = 0.0f;        // the input's loudness, eased (0 to 1) for the meter
};
}
static WelcomeState welcome;

const int GOAL_MINUTES[] = { 5, 10, 20, 30 };

static void stopListening(){
    if (!welcome.listening) return;
    if (welcome.instrument == 2) stopKeysInput();
    else stopNoteInput();
    welcome.listening = false;
}

static void startListening(const Settings& settings){
    stopListening();
    welcome.heardPitch = -1;
    welcome.inputError.clear();
    if (welcome.instrument == 2){
        startKeysInput(settings, 48);
        welcome.listening = true;
        return;
    }
    const InputRole role = welcome.instrument == 1 ? InputRole::Bass : InputRole::Guitar;
    welcome.listening = startNoteInput(settings.inputDevice, midiToFrequency(welcome.instrument == 1 ? 28.0f : 40.0f) * 0.9f, welcome.inputError,
                                       channelFor(settings, role));
}

void openWelcomeScreen(){
    welcome = WelcomeState{};
}

void closeWelcomeScreen(){
    stopListening();
}

bool welcomeBack(){
    if (welcome.step == WelcomeStep::Instrument) return true;
    stopListening();
    welcome.step = (WelcomeStep)((int)welcome.step - 1);
    return false;
}

WelcomeChoice welcomeScreen(Settings& settings){
    WelcomeChoice choice = WelcomeChoice::None;
    beginMenu("Welcome");
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const UiFonts& fonts = uiFonts();
    const float s = menuScale(), width = ImGui::GetWindowWidth(), height = ImGui::GetWindowHeight(), left = width * 0.07f;
    drawWordmark(draw, ImVec2(left, height * 0.08f), 36 * s);
    // Where it is: four dots
    for (int i = 0; i < 4; i++)
        draw->AddCircleFilled(ImVec2(width * 0.93f - (3 - i) * 22 * s, height * 0.1f), 5 * s, uiColor(i <= (int)welcome.step ? UiColor::Accent : UiColor::StaffLine), 16);
    auto heading = [&](const char* title, const char* text){
        draw->AddText(fonts.heavy, 38 * s, ImVec2(left, height * 0.2f), uiColor(UiColor::Ink), title);
        draw->AddText(fonts.text, 18 * s, ImVec2(left, height * 0.2f + 54 * s), uiColor(UiColor::Dim), text, nullptr, width * 0.55f);
    };
    const MenuListArea area = { ImVec2(left, height * 0.42f), width * 0.5f, height * 0.45f, s };
    MenuList& list = welcome.lists[(int)welcome.step];
    switch (welcome.step){
        case WelcomeStep::Instrument: {
            heading("Welcome", "lahn teaches you your instrument by playing it: it listens, shows you where each note is, and "
                               "counts every one you get right. Never played before? That's what it's for. Which instrument are you learning?");
            const std::vector<MenuRow> rows = { actionRow("Guitar"), actionRow("Bass"), actionRow("Piano") };
            const int picked = menuList(list, rows, area);
            if (picked >= 0){
                welcome.instrument = picked;
                settings.learnOnPiano = picked == 2;
                if (picked < 2) settings.heardInstrument = picked == 1 ? InputRole::Bass : InputRole::Guitar;
                welcome.step = WelcomeStep::PlugIn;
                startListening(settings);
            }
            break;
        }
        case WelcomeStep::PlugIn: {
            const bool piano = welcome.instrument == 2;
            heading(piano ? "Your keyboard" : welcome.instrument == 1 ? "Plug in your bass" : "Plug in your guitar",
                    piano ? "A MIDI keyboard plugged in is used if there's one; if not, the computer's keys are your piano (Z is a C, "
                            "the letters around it the keys next to it). Play a key."
                          : "Into your audio interface, or the computer's input with an adapter. Then play any string: the bar moves, and "
                            "the note lahn hears is named.");
            // What's heard: a meter, the note named
            if (welcome.listening){
                if (piano){
                    for (const PlayedNote& note : updateKeysInput()){
                        welcome.heardPitch = note.pitch;
                        welcome.heardAt = GetTime();
                    }
                    welcome.level += ((GetTime() - welcome.heardAt < 0.3 ? 1.0f : 0.0f) - welcome.level) * std::min(1.0f, GetFrameTime() * 10.0f);
                } else {
                    for (const PlayedNote& note : updateNoteInput()){
                        welcome.heardPitch = note.pitch;
                        welcome.heardAt = GetTime();
                    }
                    const float target = std::clamp((noteInputLevelDb() + 60.0f) / 50.0f, 0.0f, 1.0f);
                    welcome.level += (target - welcome.level) * std::min(1.0f, GetFrameTime() * 12.0f);
                }
            }
            const float meterTop = height * 0.2f + 120 * s, meterWidth = width * 0.4f;
            draw->AddRectFilled(ImVec2(left, meterTop), ImVec2(left + meterWidth, meterTop + 10 * s), uiColor(UiColor::StaffLine), 5 * s);
            draw->AddRectFilled(ImVec2(left, meterTop), ImVec2(left + meterWidth * welcome.level, meterTop + 10 * s), uiColor(UiColor::Accent), 5 * s);
            const bool heard = welcome.heardPitch >= 0;
            const std::string status = !welcome.inputError.empty() ? "The input wouldn't open: " + welcome.inputError
                                     : piano && keysInputIsMidi() ? (heard ? "MIDI keyboard found, and heard: " : "MIDI keyboard found: play a key")
                                     : heard ? "We hear you: " : "Listening...";
            draw->AddText(fonts.bold, 18 * s, ImVec2(left, meterTop + 22 * s), uiColor(!welcome.inputError.empty() ? UiColor::Bad : heard ? UiColor::Good : UiColor::Dim),
                          status.c_str());
            if (heard){
                const std::string name = std::string(pitchClassName(welcome.heardPitch)) + std::to_string(pitchOctave(welcome.heardPitch));
                const float x = left + fonts.bold->CalcTextSizeA(18 * s, FLT_MAX, 0.0f, status.c_str()).x + 8 * s;
                const float pop = 1.0f + 0.3f * std::max(0.0f, 1.0f - (float)(GetTime() - welcome.heardAt) / 0.2f);
                draw->AddText(fonts.heavy, 26 * s * pop, ImVec2(x, meterTop + 16 * s), uiColor(UiColor::Accent), name.c_str());
            }
            std::vector<MenuRow> rows = { actionRow(heard ? "Continue" : "Continue anyway") };
            if (!piano) rows.push_back(actionRow("Choose another input (Settings)"));
            const int picked = menuList(list, rows, { ImVec2(left, height * 0.5f), width * 0.5f, height * 0.35f, s });
            if (picked == 0){
                stopListening();
                welcome.step = WelcomeStep::Goal;
            } else if (picked == 1){
                stopListening();
                choice = WelcomeChoice::InputSettings;
            }
            break;
        }
        case WelcomeStep::Goal: {
            heading("A little every day", "Ten minutes a day does more than an hour once a week: your hands remember. "
                                          "How much a day? (You can change it on your profile.)");
            const std::vector<MenuRow> rows = { actionRow("5 minutes  ·  easy going"), actionRow("10 minutes  ·  steady"),
                                                actionRow("20 minutes  ·  serious"), actionRow("30 minutes  ·  intense") };
            if (list.selected < 0) list.selected = 1;
            const int picked = menuList(list, rows, area);
            if (picked >= 0){
                settings.dailyGoalMinutes = GOAL_MINUTES[picked];
                welcome.step = WelcomeStep::Ready;
            }
            break;
        }
        case WelcomeStep::Ready: {
            heading("All set", "First steps starts from nothing: where the notes are, one at a time, then their names. "
                               "Each chapter is a few minutes. Your progress, streak and achievements are on your profile. F1 shows the controls.");
            const std::vector<MenuRow> rows = { actionRow("Start First steps"), actionRow("Go to the main menu") };
            const int picked = menuList(list, rows, area);
            if (picked == 0) choice = WelcomeChoice::FirstSteps;
            if (picked == 1) choice = WelcomeChoice::MainMenu;
            break;
        }
    }
    menuScreenHint("Up/Down  choose    Enter  continue    Esc  back", s);
    ImGui::Dummy(ImVec2(1, 1));
    ImGui::End();
    return choice;
}
