#include "screens/settingsscreen.h"

#include "audio/audio.h"
#include "core/inputs.h"
#include "core/pitch.h"
#include "core/music.h"
#include "input/midi.h"
#include "input/pianokeys.h"
#include "imgui.h"
#include "raylib.h"
#include "ui/ui.h"
#include "ui/theme.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <vector>

const float PANEL_WIDTH = 760.0f;
const int FRAME_RATE_CHOICES[] = { 60, 120, 144, 240, 0 };
const char* const FRAME_RATE_LABELS[] = { "60", "120", "144", "240", "Unlimited" };
const int FRAME_RATE_CHOICE_COUNT = 5;

// Asking the system for devices can take a moment, so the lists are fetched when the screen opens
static struct {
    std::vector<std::string> outputDevices;
    std::vector<std::string> inputDevices;
    std::vector<std::string> midiDevices;
    std::string midiListening;     // the MIDI device being listened to, to show what it plays
    std::string midiError;
    int choosingKeyFor = -1;       // the piano note waiting for a key to be pressed, -1 for none
    bool usedEscape = false;
    std::vector<std::string> previewSounds;

    // The Instruments tab listens to every input of the input device while it's shown
    bool listening = false;
    std::string listeningTo;
    std::string listenError;
    int shownFrame = -10;          // the last frame the tab was drawn: leaving it stops the listening
    PitchDetector detector;
    std::vector<float> incoming;   // interleaved, from readCaptureAll
    struct Input {
        std::vector<float> window; // its latest samples, for its pitch
        float levelDb = -100.0f;
        float heardMidi = -1.0f;   // the note it hears now, -1 for none
        float lowestHz = 0.0f;     // the lowest clear note since listening started: what the instrument is
    };
    std::vector<Input> inputs;
    int detecting = -1;            // the role waiting for its instrument to be played (InputRole), -1 for none
    double detectStarted = 0.0;
    std::vector<double> loudSince; // per input: since when it's been clearly sounding, while detecting
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
    screen.midiDevices = midiDeviceNames();
    screen.midiListening = "\x01"; // none yet: the MIDI section opens the chosen device when it's first drawn
    screen.previewSounds = previewSoundNames(soundsDir);
    screen.status.clear();
}

static void stopListening(){
    if (screen.listening) stopCapture();
    screen.listening = false;
    screen.inputs.clear();
    screen.detecting = -1;
}

void closeSettingsScreen(){
    stopMidiInput();
    stopListening();
    screen.choosingKeyFor = -1;
}

// MIDI: the device, and the keys it's heard pressing right now, so the player can see it's connected and working
static void midiSection(Settings& settings){
    ImGui::SeparatorText("MIDI keyboard");
    if (ImGui::BeginCombo("MIDI", settings.midiDevice.empty() ? "The first one connected" : settings.midiDevice.c_str())){
        if (ImGui::Selectable("The first one connected", settings.midiDevice.empty())) settings.midiDevice.clear();
        for (const std::string& device : screen.midiDevices){
            if (ImGui::Selectable(device.c_str(), device == settings.midiDevice)) settings.midiDevice = device;
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    if (ImGui::Button("Refresh")){
        screen.midiDevices = midiDeviceNames();
        screen.midiListening = "\x01";
    }
    if (screen.midiListening != settings.midiDevice){
        screen.midiListening = settings.midiDevice;
        screen.midiError.clear();
        if (!startMidiInput(settings.midiDevice, screen.midiError)) stopMidiInput();
    }
    if (!midiInputActive()){
        std::string why = screen.midiError.empty() ? "no MIDI device" : screen.midiError;
        why[0] = (char)std::toupper((unsigned char)why[0]);
        ImGui::TextDisabled("%s", why.c_str());
        return;
    }
    updateMidiInput();
    std::string held;
    const bool* down = midiKeysDown();
    for (int pitch = 0; pitch < 128; pitch++) if (down[pitch]) held += TextFormat("%s%s%d", held.empty() ? "" : " ", pitchClassName(pitch), pitchOctave(pitch));
    ImGui::TextDisabled("%s: %s", midiDeviceName(), held.empty() ? "play a few keys to check it" : held.c_str());
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
    midiSection(settings);

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

bool settingsUsedEscape(){
    return screen.usedEscape;
}

// The computer keyboard's piano: a small keyboard, each note with its key. Click a note, then press its new key
// (Backspace leaves it without one, Esc cancels).
static void pianoKeysSection(Settings& settings){
    ImGui::SeparatorText("Piano on the computer keyboard");
    ImGui::TextDisabled(screen.choosingKeyFor >= 0 ? "Press the key for this note. Backspace: no key. Esc: cancel."
                                                   : "Click a note to choose its key. Up and Down move it an octave in play.");
    static const bool IS_BLACK_KEY[12] = { false, true, false, true, false, false, true, false, true, false, true, false };
    static const int WHITE_BEFORE[12] = { 0, 0, 1, 1, 2, 3, 3, 4, 4, 5, 5, 6 };
    int whites = 0;
    for (int slot = 0; slot < PIANO_KEY_SLOTS; slot++) if (!IS_BLACK_KEY[slot % 12]) whites++;
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const UiFonts& fonts = uiFonts();
    ImVec2 origin = ImGui::GetCursorScreenPos();
    float keyWidth = std::min(40.0f, ImGui::GetContentRegionAvail().x / whites), keyHeight = 96.0f;
    auto rectFor = [&](int slot){
        float whiteX = origin.x + ((slot / 12) * 7 + WHITE_BEFORE[slot % 12]) * keyWidth;
        if (!IS_BLACK_KEY[slot % 12]) return ImVec4(whiteX, origin.y, keyWidth, keyHeight);
        return ImVec4(whiteX + keyWidth * 0.7f, origin.y, keyWidth * 0.6f, keyHeight * 0.6f);
    };
    ImGui::InvisibleButton("pianokeys", ImVec2(keyWidth * whites, keyHeight));
    // Which note the mouse is on: black keys first, they sit over the white ones
    int hovered = -1;
    ImVec2 mouse = ImGui::GetMousePos();
    for (int pass = 0; pass < 2 && hovered < 0; pass++){
        for (int slot = 0; slot < PIANO_KEY_SLOTS; slot++){
            if (IS_BLACK_KEY[slot % 12] != (pass == 0)) continue;
            ImVec4 r = rectFor(slot);
            if (mouse.x >= r.x && mouse.x < r.x + r.z && mouse.y >= r.y && mouse.y < r.y + r.w){ hovered = slot; break; }
        }
    }
    if (ImGui::IsItemClicked() && hovered >= 0) screen.choosingKeyFor = hovered;
    for (int pass = 0; pass < 2; pass++){
        for (int slot = 0; slot < PIANO_KEY_SLOTS; slot++){
            bool black = IS_BLACK_KEY[slot % 12];
            if (black != (pass == 1)) continue;
            ImVec4 r = rectFor(slot);
            bool choosing = slot == screen.choosingKeyFor;
            ImU32 fill = choosing ? uiColor(UiColor::Accent) : black ? IM_COL32(30, 30, 34, 255) : IM_COL32(255, 255, 255, 255);
            if (!choosing && slot == hovered) fill = black ? IM_COL32(70, 70, 76, 255) : uiColor(UiColor::Accent, 0.25f);
            draw->AddRectFilled(ImVec2(r.x + 0.5f, r.y), ImVec2(r.x + r.z - 0.5f, r.y + r.w), fill, 3.0f);
            if (!black) draw->AddRect(ImVec2(r.x + 0.5f, r.y), ImVec2(r.x + r.z - 0.5f, r.y + r.w), uiColor(UiColor::StaffLine), 3.0f);
            std::string label = choosing ? "?" : settings.pianoKeys[slot] == "none" ? "" : settings.pianoKeys[slot];
            ImVec2 size = fonts.bold ? fonts.bold->CalcTextSizeA(15.0f, FLT_MAX, 0.0f, label.c_str()) : ImVec2(0, 0);
            ImU32 ink = choosing || black ? IM_COL32(255, 255, 255, 255) : uiColor(UiColor::Ink);
            draw->AddText(fonts.bold, 15.0f, ImVec2(r.x + (r.z - size.x) / 2, r.y + r.w - size.y - 6.0f), ink, label.c_str());
        }
    }
    // Waiting for a key: the first one pressed that can play a note
    screen.usedEscape = false;
    if (screen.choosingKeyFor >= 0){
        if (ImGui::IsKeyPressed(ImGuiKey_Escape)){
            screen.choosingKeyFor = -1;
            screen.usedEscape = true;
        } else if (ImGui::IsKeyPressed(ImGuiKey_Backspace)){
            settings.pianoKeys[screen.choosingKeyFor] = "none";
            screen.choosingKeyFor = -1;
        } else if (int code = pianoKeyJustPressed()){
            bindPianoKey(settings.pianoKeys, screen.choosingKeyFor, pianoKeyName(code));
            screen.choosingKeyFor = -1;
        }
    }
    if (ImGui::Button("Reset to the default keys")){
        settings.pianoKeys = defaultPianoKeys();
        screen.choosingKeyFor = -1;
    }
}

// Reads what every input is hearing: its level, its note now, and the lowest note it's heard
static void listenToInputs(const Settings& settings){
    if (!screen.listening || screen.listeningTo != settings.inputDevice){
        stopListening();
        screen.listeningTo = settings.inputDevice;
        screen.listenError.clear();
        if (!startCapture(settings.inputDevice, screen.listenError)) return;
        screen.listening = true;
        initPitchDetector(screen.detector, captureSampleRate(), 30.0f, 1400.0f);
        int window = pitchWindowSize(screen.detector), channels = captureChannels();
        screen.inputs.assign(channels, {});
        for (auto& input : screen.inputs) input.window.assign(window, 0.0f);
        screen.incoming.assign((size_t)window * channels, 0.0f);
    }
    const int channels = (int)screen.inputs.size(), window = (int)screen.inputs[0].window.size();
    int got;
    bool fresh = false;
    while ((got = readCaptureAll(screen.incoming.data(), window)) > 0){
        fresh = true;
        for (int c = 0; c < channels; c++){
            std::vector<float>& samples = screen.inputs[c].window;
            std::memmove(samples.data(), samples.data() + got, (window - got) * sizeof(float));
            for (int i = 0; i < got; i++) samples[window - got + i] = screen.incoming[(size_t)i * channels + c];
        }
    }
    if (!fresh) return;
    for (auto& input : screen.inputs){
        float sum = 0.0f;
        for (float sample : input.window) sum += sample * sample;
        input.levelDb = 20.0f * std::log10(std::max(std::sqrt(sum / window), 1e-6f));
        input.heardMidi = -1.0f;
        if (input.levelDb < -45.0f) continue;
        PitchResult pitch = detectPitch(screen.detector, input.window.data(), window);
        if (pitch.frequency <= 0.0f) continue;
        input.heardMidi = frequencyToMidi(pitch.frequency);
        if (input.lowestHz <= 0.0f || pitch.frequency < input.lowestHz) input.lowestHz = pitch.frequency;
    }
}

static int& roleChannel(Settings& settings, InputRole role){
    return role == InputRole::Guitar ? settings.guitarChannel : role == InputRole::Bass ? settings.bassChannel : settings.voiceChannel;
}

// The inputs, and which instrument is on which: a meter and what each one hears, then each instrument's input,
// found by playing it (Detect) or chosen by hand
static void instrumentsTab(Settings& settings){
    screen.shownFrame = (int)ImGui::GetFrameCount();
    listenToInputs(settings);
    ImGui::SeparatorText("Inputs");
    ImGui::TextDisabled("%s", settings.inputDevice.empty() ? "The system's default input device (Audio tab to change it)"
                                                           : ("On " + settings.inputDevice + " (Audio tab to change it)").c_str());
    if (!screen.listening){
        ImGui::TextColored(uiColorVec(UiColor::Bad), "Can't listen: %s", screen.listenError.c_str());
        return;
    }
    ImDrawList* draw = ImGui::GetWindowDrawList();
    for (int c = 0; c < (int)screen.inputs.size(); c++){
        const auto& input = screen.inputs[c];
        ImGui::Text("Input %d", c + 1);
        ImGui::SameLine(90);
        ImVec2 at = ImGui::GetCursorScreenPos();
        float meterWidth = 200.0f, fill = std::clamp((input.levelDb + 60.0f) / 60.0f, 0.0f, 1.0f);
        draw->AddRectFilled(ImVec2(at.x, at.y + 6), ImVec2(at.x + meterWidth, at.y + 14), uiColor(UiColor::StaffLine), 3.0f);
        if (fill > 0.02f) draw->AddRectFilled(ImVec2(at.x, at.y + 6), ImVec2(at.x + meterWidth * fill, at.y + 14), uiColor(UiColor::Good), 3.0f);
        ImGui::Dummy(ImVec2(meterWidth + 12, 18));
        ImGui::SameLine();
        std::string now = input.heardMidi >= 0.0f ? TextFormat("%s%d", pitchClassName((int)std::lround(input.heardMidi)), pitchOctave((int)std::lround(input.heardMidi))) : "-";
        std::string lowest = input.lowestHz > 0.0f
            ? TextFormat("lowest %s%d: %s", pitchClassName((int)std::lround(frequencyToMidi(input.lowestHz))),
                         pitchOctave((int)std::lround(frequencyToMidi(input.lowestHz))), guessInstrument(input.lowestHz).c_str())
            : "play its lowest string";
        ImGui::TextDisabled("%-4s  %s", now.c_str(), lowest.c_str());
    }
    if (ImGui::Button("Listen again")) for (auto& input : screen.inputs) input.lowestHz = 0.0f;

    // Detecting: the input that's clearly sounding, for half a second, while the player plays the instrument
    if (screen.detecting >= 0){
        double now = GetTime();
        screen.loudSince.resize(screen.inputs.size(), -1.0);
        int loudest = -1;
        for (int c = 0; c < (int)screen.inputs.size(); c++){
            bool loud = screen.inputs[c].levelDb > -35.0f;
            if (!loud) screen.loudSince[c] = -1.0;
            else if (screen.loudSince[c] < 0.0) screen.loudSince[c] = now;
            if (loud && (loudest < 0 || screen.inputs[c].levelDb > screen.inputs[loudest].levelDb)) loudest = c;
        }
        if (loudest >= 0 && now - screen.loudSince[loudest] > 0.5){
            roleChannel(settings, (InputRole)screen.detecting) = screen.inputs.size() > 1 ? loudest : -1;
            screen.detecting = -1;
        } else if (now - screen.detectStarted > 10.0){
            screen.detecting = -1; // nothing played: give up quietly
        }
    }

    ImGui::SeparatorText("Instruments");
    for (InputRole role : { InputRole::Guitar, InputRole::Bass, InputRole::Voice }){
        ImGui::PushID((int)role);
        int& channel = roleChannel(settings, role);
        std::string current = channel < 0 ? "All inputs mixed" : TextFormat("Input %d", channel + 1);
        ImGui::SetNextItemWidth(220);
        if (ImGui::BeginCombo(inputRoleName(role), current.c_str())){
            if (ImGui::Selectable("All inputs mixed", channel < 0)) channel = -1;
            for (int c = 0; c < (int)screen.inputs.size(); c++){
                if (ImGui::Selectable(TextFormat("Input %d", c + 1), channel == c)) channel = c;
            }
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        if (screen.detecting == (int)role) ImGui::TextColored(uiColorVec(UiColor::Accent), "Play your %s...", inputRoleName(role));
        else if (ImGui::Button("Detect")){
            screen.detecting = (int)role;
            screen.detectStarted = GetTime();
            screen.loudSince.assign(screen.inputs.size(), -1.0);
        }
        // What that input has heard, against what it's meant to be
        if (channel >= 0 && channel < (int)screen.inputs.size() && !fitsRole(role, screen.inputs[channel].lowestHz)){
            ImGui::SameLine();
            ImGui::TextColored(uiColorVec(UiColor::Bad), "sounds like %s", guessInstrument(screen.inputs[channel].lowestHz).c_str());
        }
        ImGui::PopID();
    }
    ImGui::TextDisabled("Each instrument is judged from its own input, so they're never mixed together.");
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
        if (ImGui::BeginTabItem("Piano keys")){ pianoKeysSection(settings); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Instruments")){ instrumentsTab(settings); ImGui::EndTabItem(); }
        ImGui::EndTabBar();
        if (screen.listening && screen.shownFrame != (int)ImGui::GetFrameCount()) stopListening(); // left the Instruments tab
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
