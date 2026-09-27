#include "screens/settingsscreen.h"

#include "audio/audio.h"
#include "raylib.h"
#include "ui/ui.h"
#include "ui/theme.h"

#include <vector>

const float PANEL_WIDTH = 760.0f;
const int FRAME_RATE_CHOICES[] = { 60, 120, 144, 240, 0 };
const char* const FRAME_RATE_LABELS[] = { "60", "120", "144", "240", "Unlimited" };
const int FRAME_RATE_CHOICE_COUNT = 5;

// Asking the system for devices can take a moment, so the lists are fetched when the screen opens
static struct {
    std::vector<std::string> outputDevices;
    std::vector<std::string> inputDevices;
    std::vector<std::string> previewSounds;
    std::string status;   // result of the last change, e.g. a device that failed to open
    bool statusIsError = false;
} screen;

void applyDisplaySettings(const Settings& settings){
    if (settings.fullscreen != IsWindowState(FLAG_BORDERLESS_WINDOWED_MODE)) ToggleBorderlessWindowed();
    SetTargetFPS(settings.frameRateLimit);
}

void openSettingsScreen(const std::string& soundsDir){
    screen.outputDevices = outputDeviceNames();
    screen.inputDevices = inputDeviceNames();
    screen.previewSounds = previewSoundNames(soundsDir);
    screen.status.clear();
}

static void setStatus(const std::string& text, bool isError){
    screen.status = text;
    screen.statusIsError = isError;
}

// A dropdown of device names with "System default" first; returns true when the choice changed
static bool deviceCombo(const char* label, std::string& current, const std::vector<std::string>& devices){
    bool changed = false;
    if (ImGui::BeginCombo(label, current.empty() ? "System default" : current.c_str())){
        if (ImGui::Selectable("System default", current.empty())){
            changed = !current.empty();
            current.clear();
        }
        for (const std::string& device : devices){
            if (ImGui::Selectable(device.c_str(), device == current)){
                changed = device != current;
                current = device;
            }
        }
        ImGui::EndCombo();
    }
    return changed;
}

// Plays an A major chord: three notes at once, to hear the sound and that notes can overlap
static void playTestChord(){
    playPreview(220.00f);
    playPreview(277.18f);
    playPreview(329.63f);
}

static void audioTab(Settings& settings, const std::string& soundsDir){
    ImGui::SeparatorText("Devices");
    if (deviceCombo("Output", settings.outputDevice, screen.outputDevices)){
        std::string error;
        if (setOutputDevice(settings.outputDevice, error)) setStatus(std::string("Playing through ") + outputDeviceName(), false);
        else setStatus(error, true);
    }
    // The input device is opened when something listens (the tuner), so choosing it here is enough
    deviceCombo("Input", settings.inputDevice, screen.inputDevices);
    ImGui::TextDisabled("Audio system: %s", audioBackendName());

    ImGui::SeparatorText("Volume");
    if (ImGui::SliderFloat("Master", &settings.masterVolume, 0.0f, 1.0f, "%.2f")) setMasterVolume(settings.masterVolume);
    if (ImGui::SliderFloat("Preview sounds", &settings.previewVolume, 0.0f, 1.0f, "%.2f")) setPreviewVolume(settings.previewVolume);

    ImGui::SeparatorText("Preview sound");
    if (ImGui::BeginCombo("Sound", settings.previewSound.c_str())){
        for (const std::string& name : screen.previewSounds){
            if (ImGui::Selectable(name.c_str(), name == settings.previewSound)){
                std::string error;
                if (setPreviewSound(name, soundsDir, error)){
                    settings.previewSound = name;
                    setStatus(previewSoundHasPitch() ? "" : "This sound has no clear pitch: it plays the same for every note", false);
                    playTestChord();
                } else {
                    setStatus(error, true);
                }
            }
        }
        ImGui::EndCombo();
    }
    if (ImGui::Button("Test")) playTestChord();
    ImGui::SameLine();
    if (ImGui::Button("Open sounds folder")) openFolder(soundsDir);
    ImGui::SameLine();
    if (ImGui::Button("Refresh")) openSettingsScreen(soundsDir);
    ImGui::TextWrapped("Add your own: put .wav or .flac files in the sounds folder (mp3 works, but usually starts late). "
                       "Sounds with a clear pitch are tuned to each note.");
}

static void displayTab(Settings& settings){
    ImGui::SeparatorText("Notes");
    // Any mix, stacked; unticking the last one ticks it straight back, since something must show the notes
    NoteViews& views = settings.noteViews;
    ImGui::TextUnformatted("Show notes as (any mix, stacked)");
    if (ImGui::Checkbox("Sheet music", &views.staff) && !views.any()) views.staff = true;
    ImGui::SameLine();
    if (ImGui::Checkbox("Tab", &views.tab) && !views.any()) views.tab = true;
    ImGui::SameLine();
    if (ImGui::Checkbox("Highway", &views.highway) && !views.any()) views.highway = true;

    ImGui::BeginDisabled(!views.highway);
    ImGui::TextUnformatted("The highway");
    int direction = views.highwayFalls ? 1 : 0;
    ImGui::RadioButton("Scrolls across", &direction, 0);
    ImGui::SameLine();
    ImGui::RadioButton("Falls down (strings side by side)", &direction, 1);
    views.highwayFalls = direction == 1;
    ImGui::EndDisabled();

    ImGui::TextUnformatted("String order on the highway and in the editor");
    int stringOrder = settings.lowStringOnTop ? 0 : 1;
    ImGui::RadioButton("Low E at the top (left when falling)", &stringOrder, 0);
    ImGui::SameLine();
    ImGui::RadioButton("Low E at the bottom (right when falling)", &stringOrder, 1);
    settings.lowStringOnTop = stringOrder == 0;

    ImGui::SeparatorText("Colors");
    int colors = settings.darkTheme ? 1 : 0;
    ImGui::RadioButton("Light", &colors, 0);
    ImGui::SameLine();
    ImGui::RadioButton("Dark", &colors, 1);
    if ((colors == 1) != settings.darkTheme){
        settings.darkTheme = colors == 1;
        setTheme(settings.darkTheme ? ThemeMode::Dark : ThemeMode::Light); // at once, so the choice can be seen
    }

    ImGui::SeparatorText("Window");
    if (ImGui::Checkbox("Fullscreen", &settings.fullscreen)) applyDisplaySettings(settings);

    int choice = FRAME_RATE_CHOICE_COUNT - 1;
    for (int i = 0; i < FRAME_RATE_CHOICE_COUNT; i++) if (FRAME_RATE_CHOICES[i] == settings.frameRateLimit) choice = i;
    if (ImGui::Combo("Frame rate limit", &choice, FRAME_RATE_LABELS, FRAME_RATE_CHOICE_COUNT)){
        settings.frameRateLimit = FRAME_RATE_CHOICES[choice];
        applyDisplaySettings(settings);
    }
    ImGui::TextDisabled("Higher frame rates make notes move more smoothly, if your screen can show them.");
}

static void gameplayTab(Settings& settings, SettingsChoice& choice){
    ImGui::SeparatorText("Playing");
    int input = settings.playWithInstrument ? 1 : 0;
    ImGui::RadioButton("Keyboard (keys 1 to 6)", &input, 0);
    ImGui::SameLine();
    ImGui::RadioButton("My instrument (input device)", &input, 1);
    settings.playWithInstrument = input == 1;
    ImGui::SliderFloat("Note speed", &settings.noteSpeed, 100.0f, 1500.0f, "%.0f px/s");
    ImGui::TextDisabled("Faster notes are spread further apart. Timing is judged the same at any speed.");

    ImGui::SeparatorText("Latency");
    ImGui::SliderInt("Global offset", &settings.globalOffsetMs, -500, 500, "%d ms");
    if (ImGui::Button("Calibrate by tapping")) choice = SettingsChoice::CalibrateTapping;
    ImGui::SameLine();
    ImGui::TextDisabled("How late sound reaches you (Bluetooth, slow drivers)");
    ImGui::SliderInt("Input offset", &settings.inputOffsetMs, -500, 500, "%d ms");
    if (ImGui::Button("Calibrate my instrument")) choice = SettingsChoice::CalibrateInstrument;
    ImGui::SameLine();
    ImGui::TextDisabled("Your input device's own delay. Tap first");
}

SettingsChoice settingsScreen(Settings& settings, const std::string& soundsDir, const std::string& error){
    SettingsChoice choice = SettingsChoice::None;
    beginMenu("Settings");
    menuTitle("Settings");

    ImGui::SetCursorPosX((ImGui::GetWindowWidth() - PANEL_WIDTH) / 2);
    ImGui::BeginChild("SettingsPanel", ImVec2(PANEL_WIDTH, ImGui::GetContentRegionAvail().y - 110));
    ImGui::PushItemWidth(-220); // room for the labels on the right
    if (ImGui::BeginTabBar("SettingsTabs")){
        if (ImGui::BeginTabItem("Audio")){ audioTab(settings, soundsDir); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Display")){ displayTab(settings); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Gameplay")){ gameplayTab(settings, choice); ImGui::EndTabItem(); }
        ImGui::EndTabBar();
    }
    ImGui::PopItemWidth();
    if (!screen.status.empty()){
        ImGui::Dummy(ImVec2(0, 6));
        if (screen.statusIsError) ImGui::TextColored(uiColorVec(UiColor::Bad), "%s", screen.status.c_str());
        else ImGui::TextWrapped("%s", screen.status.c_str());
    }
    if (!error.empty()) ImGui::TextColored(uiColorVec(UiColor::Bad), "%s", error.c_str());
    ImGui::EndChild();

    if (menuButton("Back")) choice = SettingsChoice::Back;
    ImGui::End();
    return choice;
}
