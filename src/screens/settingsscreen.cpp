#include "screens/settingsscreen.h"

#include "audio/audio.h"
#include "core/inputs.h"
#include "core/pitch.h"
#include "core/music.h"
#include "input/midi.h"
#include "input/pianokeys.h"
#include "screens/tonewizard.h"
#include "imgui.h"
#include "imgui_internal.h"
#include "raylib.h"
#include "ui/ui.h"
#include "ui/menulist.h"
#include "ui/pianoview.h"
#include "ui/rewards.h"
#include "ui/settingsui.h"
#include "ui/theme.h"

#include <algorithm>
#include <cstdarg>
#include <cctype>
#include <cmath>
#include <cstring>
#include <vector>

const int FRAME_RATE_CHOICES[] = { 60, 120, 144, 240, 0 };
const int FRAME_RATE_CHOICE_COUNT = 5;

// The sections, listed on the left; the one chosen is shown on the card on the right
enum class Section { Audio, Instruments, Gameplay, Display, PianoKeys };
const char* const SECTION_NAMES[] = { "Audio", "Instruments", "Gameplay", "Display", "Piano keys" };
const int SECTION_COUNT = 5;
const float SECTION_ROW = 46.0f; // at a 720-pixel-tall window

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
        NoiseFloor floor;          // its level with nothing played: playing is judged against it
        float heardMidi = -1.0f;   // the note it hears now, -1 for none
        float lowestHz = 0.0f;     // the lowest clear note since listening started: what the instrument is
    };
    std::vector<Input> inputs;
    int detecting = -1;            // the role waiting for its instrument to be played (InputRole), -1 for none
    double detectStarted = 0.0;
    std::vector<double> loudSince; // per input: since when it's been clearly sounding, while detecting
    std::string status;   // result of the last change, e.g. a device that failed to open
    std::string monitorError; // why the instrument can't be heard, if it can't
    bool statusIsError = false;
    Section section = Section::Audio;
    bool popupWasOpen = false;   // a dropdown was open: Esc closed it, it doesn't leave the screen
} screen;

void applyDisplaySettings(const Settings& settings){
    if (settings.fullscreen != IsWindowState(FLAG_BORDERLESS_WINDOWED_MODE)){
        ToggleBorderlessWindowed();
        // raylib keeps its borderless window above every other: Alt+Tab would switch to a window hidden behind it
        if (IsWindowState(FLAG_WINDOW_TOPMOST)) ClearWindowState(FLAG_WINDOW_TOPMOST);
    }
    SetTargetFPS(settings.frameRateLimit);
}

void applyMonitor(const Settings& settings, std::string& error){
    std::vector<int> inputs;
    for (int channel : { settings.guitarChannel, settings.bassChannel }){
        if (channel >= 0 && std::count(inputs.begin(), inputs.end(), channel) == 0) inputs.push_back(channel);
    }
    applyTone(settings);
    setMonitorSynth(settings.monitorSynth);
    error.clear();
    setMonitor(settings.monitorOn, settings.inputDevice, inputs, settings.voiceChannel, error);
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

static void setStatus(const std::string& text, bool isError){
    screen.status = text;
    screen.statusIsError = isError;
}

// A device list for a dropdown: `first` (the system's default, or any), then the devices; one set but not found is
// kept, marked, so the setting isn't silently replaced
static std::vector<std::string> deviceOptions(const char* first, const std::vector<std::string>& devices, const std::string& current, int& chosen){
    std::vector<std::string> options = { first };
    options.insert(options.end(), devices.begin(), devices.end());
    chosen = 0;
    if (current.empty()) return options;
    for (int i = 1; i < (int)options.size(); i++) if (options[i] == current) chosen = i;
    if (chosen == 0){
        options.push_back(current + "  (not connected)");
        chosen = (int)options.size() - 1;
    }
    return options;
}

// What a chosen option names: "" for the first (the default), the device otherwise
static std::string deviceChosen(const std::vector<std::string>& options, int chosen, const std::string& current){
    if (chosen <= 0) return "";
    const std::string& option = options[chosen];
    return option.size() > current.size() && option.compare(0, current.size(), current) == 0 && option.find("  (not connected)") != std::string::npos ? current : option;
}

// Plays an A major chord: three notes at once, to hear the sound and that notes can overlap
static void playTestChord(){
    playPreview(220.00f);
    playPreview(277.18f);
    playPreview(329.63f);
}

// A 0..1 volume as a percent slider
static bool percentSlider(const char* label, const char* hint, float* value){
    float percent = *value * 100.0f;
    if (!settingSlider(label, hint, &percent, 0.0f, 100.0f, "%.0f%%")) return false;
    *value = percent / 100.0f;
    return true;
}

static void audioSection(Settings& settings, const std::string& soundsDir, SettingsChoice& choice){
    settingsGroup("DEVICES");
    int chosen = 0;
    std::vector<std::string> outputs = deviceOptions("System default", screen.outputDevices, settings.outputDevice, chosen);
    if (outputIsAsio()){
        settingInfo("Output", "Everything plays through the ASIO driver your input is on", outputDeviceName());
    } else if (settingDropdown("Output", "Where lahn's sound comes out", &chosen, outputs, []{ screen.outputDevices = outputDeviceNames(); })){
        settings.outputDevice = deviceChosen(outputs, chosen, settings.outputDevice);
        std::string error;
        if (setOutputDevice(settings.outputDevice, error)) setStatus("", false);
        else setStatus(error, true);
    }
    std::vector<std::string> inputs = deviceOptions("System default", screen.inputDevices, settings.inputDevice, chosen);
    if (settingDropdown("Input", "Your audio interface or microphone. An ASIO driver (\"ASIO: ...\") is the fastest.", &chosen, inputs,
                        []{ screen.inputDevices = inputDeviceNames(); })){
        settings.inputDevice = deviceChosen(inputs, chosen, settings.inputDevice);
        applyMonitor(settings, screen.monitorError);
    }
    settingInfo("Audio system", nullptr, audioBackendName());

    // MIDI: the device, and the keys it's heard pressing right now, so the player can see it's connected and working
    settingsGroup("MIDI KEYBOARD");
    std::vector<std::string> midis = deviceOptions("The first one connected", screen.midiDevices, settings.midiDevice, chosen);
    if (settingDropdown("MIDI keyboard", "For playing piano parts", &chosen, midis, []{ screen.midiDevices = midiDeviceNames(); })){
        settings.midiDevice = deviceChosen(midis, chosen, settings.midiDevice);
    }
    if (screen.midiListening != settings.midiDevice){
        screen.midiListening = settings.midiDevice;
        screen.midiError.clear();
        if (!startMidiInput(settings.midiDevice, screen.midiError)) stopMidiInput();
    }
    if (!midiInputActive()){
        std::string why = screen.midiError.empty() ? "no MIDI keyboard" : screen.midiError;
        why[0] = (char)std::toupper((unsigned char)why[0]);
        settingInfo("Heard", nullptr, why.c_str());
    } else {
        updateMidiInput();
        std::string held;
        const bool* down = midiKeysDown();
        for (int pitch = 0; pitch < 128; pitch++) if (down[pitch]) held += TextFormat("%s%s%d", held.empty() ? "" : " ", pitchClassName(pitch), pitchOctave(pitch));
        settingInfo("Heard", midiDeviceName(), held.empty() ? "Play a few keys to check it" : held.c_str(), held.empty() ? UiColor::Dim : UiColor::Accent);
    }

    // Hearing the instrument through lahn, wherever the player is
    settingsGroup("HEAR MY INSTRUMENT");
    if (settingToggle("Hear my instrument", "Through lahn's speakers, wherever you are in the game", &settings.monitorOn)){
        applyMonitor(settings, screen.monitorError);
    }
    ImGui::BeginDisabled(!settings.monitorOn);
    int sound = settings.monitorSynth ? 1 : 0;
    if (settingSegments("Sound", settings.monitorSynth ? "The notes lahn hears, played on a synth bass: a little later than your own sound"
                                                       : "Your own sound through your tone: no delay, with ASIO or Windows' fast mode",
                        &sound, { "My sound", "Synth bass" })){
        settings.monitorSynth = sound == 1;
        setMonitorSynth(settings.monitorSynth);
    }
    if (percentSlider("Volume", nullptr, &settings.monitorVolume)) applyTone(settings);
    ImGui::EndDisabled();
    // The tones, one per instrument: chosen here, made in the tone wizard. They shape the instrument's own sound only.
    std::vector<std::string> toneList = toneNames();
    const float wizardWidth = 130.0f * menuScale();
    for (InputRole role : { InputRole::Bass, InputRole::Guitar }){
        bool bass = role == InputRole::Bass;
        ImGui::PushID(bass ? "bass" : "guitar");
        SettingControl toneRow = settingRow(bass ? "Bass tone" : "Guitar tone", bass ? "Pedals, an amp and a room: your bass, shaped"
                                                                                      : "Your guitar's own, apart from the bass's",
                                            settingsControlHeight());
        std::string& name = settings.toneFor(role);
        int toneChosen = 0;
        for (int i = 0; i < (int)toneList.size(); i++) if (toneList[i] == name) toneChosen = i;
        ImGui::BeginDisabled(settings.monitorSynth || !settings.monitorOn);
        if (settingsDropdownAt("tone", toneRow.min, ImVec2(toneRow.max.x - wizardWidth - 10.0f * menuScale(), toneRow.max.y), &toneChosen, toneList)){
            name = toneList[toneChosen];
            hearInstrument(settings, role); // heard as it's chosen
        }
        ImGui::EndDisabled();
        // The wizard opens either way: it offers to switch to the instrument's own sound
        if (settingsButtonAt("wizard", ImVec2(toneRow.max.x - wizardWidth, toneRow.min.y), toneRow.max, "Tone wizard")){
            settings.heardInstrument = role;
            choice = SettingsChoice::ToneWizard;
        }
        ImGui::PopID();
    }
    if (!screen.monitorError.empty()) settingNote(screen.monitorError.c_str(), UiColor::Bad);

    settingsGroup("VOLUME");
    if (percentSlider("Everything", nullptr, &settings.masterVolume)) setMasterVolume(settings.masterVolume);
    if (percentSlider("Preview sounds", "Notes you play on the keyboard, drums and ear training (the song editor has its own volumes)", &settings.previewVolume)) setPreviewVolume(settings.previewVolume);
    percentSlider("Backing band", "The band playing along in the timed drills", &settings.bandVolume);
    if (percentSlider("Metronome", "The clicks: counting in, and the metronome in drills and practice", &settings.clickVolume)) setClickVolume(settings.clickVolume);
    if (ImGui::IsItemDeactivatedAfterEdit()) playClickAt(audioTime() + 0.05, true); // heard at the level just set
    if (percentSlider("Rewards", "The chimes of XP, levels, achievements and goals met", &settings.rewardVolume)) setRewardVolume(settings.rewardVolume);
    if (percentSlider("Hit sound", "On every note you hit with your instrument", &settings.hitSoundVolume)) setHitSoundVolume(settings.hitSoundVolume);
    // Heard at the level just set, the way it's set
    auto hearHitSound = [&]{
        if (settings.hitSoundIsNote) playStringNote(midiToFrequency(settings.heardInstrument == InputRole::Guitar ? 52.0f : 40.0f),
                                                    settings.heardInstrument != InputRole::Guitar, 0.6f, settings.hitSoundVolume);
        else playHitSound(true);
    };
    if (ImGui::IsItemDeactivatedAfterEdit()) hearHitSound();
    int hitKind = settings.hitSoundIsNote ? 0 : 1;
    if (settingSegments("Hit sound is", "The note you hit, on your instrument's own sound (as in the song editor), or a drop", &hitKind, { "The note", "A drop" })){
        settings.hitSoundIsNote = hitKind == 0;
        hearHitSound();
    }

    settingsGroup("PREVIEW SOUND");
    chosen = 0;
    for (int i = 0; i < (int)screen.previewSounds.size(); i++) if (screen.previewSounds[i] == settings.previewSound) chosen = i;
    if (settingDropdown("Sound", "Pitched sounds are tuned to each note", &chosen, screen.previewSounds,
                        [soundsDir]{ screen.previewSounds = previewSoundNames(soundsDir); })){
        std::string error;
        if (setPreviewSound(screen.previewSounds[chosen], soundsDir, error)){
            settings.previewSound = screen.previewSounds[chosen];
            setStatus(previewSoundHasPitch() ? "" : "This sound has no clear pitch: it plays the same for every note", false);
            playTestChord();
        } else {
            setStatus(error, true);
        }
    }
    switch (settingButtons("Your own sounds", "Put .wav or .flac files in the sounds folder", { "Test", "Open folder" })){
        case 0: playTestChord(); break;
        case 1: openFolder(soundsDir); break;
        default: break;
    }
}

static void displaySection(Settings& settings){
    // Any mix, stacked; turning the last one off turns it straight back on, since something must show the notes
    settingsGroup("SHOW NOTES AS");
    NoteViews& views = settings.noteViews;
    if (settingToggle("Sheet music", nullptr, &views.staff) && !views.any()) views.staff = true;
    if (settingToggle("Neck", "Rings closing onto each note's place on the fretboard", &views.neck) && !views.any()) views.neck = true;
    ImGui::BeginDisabled(!views.neck);
    int label = (int)views.label;
    if (settingSegments("On each note", "The note's name helps you learn the neck", &label,
                        { "Fret", "Note name", "Both" })) views.label = (NoteLabel)label;
    int range = views.wholeNeck ? 0 : 1;
    if (settingSegments("Neck shown", "The whole neck, as on your instrument, or only the frets the song uses", &range,
                        { "Whole neck", "The song's frets" })) views.wholeNeck = range == 0;
    ImGui::EndDisabled();
    int order = settings.lowStringOnTop ? 0 : 1;
    if (settingSegments("Lowest string", "On the neck and in the editor", &order, { "On top", "At the bottom" })){
        settings.lowStringOnTop = order == 0;
    }

    int video = settings.songVideo ? 0 : 1;
    if (settingSegments("Song videos", "A song's video, when it has one, behind the notes", &video, { "Shown", "Hidden" })){
        settings.songVideo = video == 0;
    }

    settingsGroup("LOOK");
    int colors = settings.darkTheme ? 1 : 0;
    if (settingSegments("Theme", nullptr, &colors, { "Light", "Dark" })){
        settings.darkTheme = colors == 1;
        setTheme(settings.darkTheme ? ThemeMode::Dark : ThemeMode::Light); // at once, so the choice can be seen
    }

    settingsGroup("ACCESSIBILITY");
    bool accessChanged = settingToggle("Colour-blind colours", "Hits and misses in blue and orange, told apart with any colour vision", &settings.colorBlind);
    accessChanged = settingToggle("Less motion", "No sparks, no sliding cards, screens change at once", &settings.reduceMotion) || accessChanged;
    const float SCALES[] = { 0.9f, 1.0f, 1.15f, 1.3f };
    int scale = 1;
    for (int i = 0; i < 4; i++) if (std::fabs(settings.uiScale - SCALES[i]) < 0.01f) scale = i;
    if (settingSegments("Interface size", "Everything in the menus and the learning screens", &scale, { "90%", "100%", "115%", "130%" })){
        settings.uiScale = SCALES[scale];
        accessChanged = true;
    }
    if (accessChanged) setAccessibility(settings.colorBlind, settings.reduceMotion, settings.uiScale);

    settingsGroup("WINDOW");
    if (settingToggle("Fullscreen", nullptr, &settings.fullscreen)) applyDisplaySettings(settings);
    int rate = FRAME_RATE_CHOICE_COUNT - 1;
    for (int i = 0; i < FRAME_RATE_CHOICE_COUNT; i++) if (FRAME_RATE_CHOICES[i] == settings.frameRateLimit) rate = i;
    if (settingSegments("Frame rate", "Higher is smoother, if your screen can show it", &rate, { "60", "120", "144", "240", "Any" })){
        settings.frameRateLimit = FRAME_RATE_CHOICES[rate];
        applyDisplaySettings(settings);
    }
}

bool settingsUsedEscape(){
    return screen.usedEscape || screen.popupWasOpen; // Esc cancelled choosing a key, or closed a dropdown
}

// The computer keyboard's piano: a small keyboard, each note with its key. Click a note, then press its new key
// (Backspace leaves it without one, Esc cancels).
static void pianoKeysSection(Settings& settings){
    const float s = menuScale();
    settingsGroup("PIANO ON THE COMPUTER KEYBOARD");
    settingNote(screen.choosingKeyFor >= 0 ? "Press the key for this note. Backspace: no key. Esc: cancel."
                                           : "Click a note to choose its key. In a song, Up and Down move the keys an octave.",
                screen.choosingKeyFor >= 0 ? UiColor::Accent : UiColor::Dim);
    float keyWidth = std::min(40.0f * s, ImGui::GetContentRegionAvail().x / pianoWhiteKeys(PIANO_KEY_SLOTS)), keyHeight = 110.0f * s;
    ImVec2 origin = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("pianokeys", ImVec2(keyWidth * pianoWhiteKeys(PIANO_KEY_SLOTS), keyHeight));
    bool clicked = ImGui::IsItemClicked();
    int hovered = drawPianoKeys(origin, keyWidth, keyHeight, PIANO_KEY_SLOTS, [&](int slot, bool hovered){
        PianoKeyStyle look;
        bool choosing = slot == screen.choosingKeyFor;
        if (choosing) look.fill = uiColor(UiColor::Accent);
        else if (hovered) look.fill = pianoKeyIsBlack(slot) ? IM_COL32(70, 70, 76, 255) : uiColor(UiColor::Accent, 0.25f);
        if (hovered && !choosing && !pianoKeyIsBlack(slot)) look.ink = uiColor(UiColor::Ink);
        look.label = choosing ? "?" : settings.pianoKeys[slot] == "none" ? "" : settings.pianoKeys[slot];
        return look;
    });
    if (clicked && hovered >= 0) screen.choosingKeyFor = hovered;
    ImGui::Dummy(ImVec2(0, 12 * s));
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
    if (settingButton("Default keys", "Every note back on the key it started on", "Reset")){
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
        trackNoiseFloor(input.floor, input.levelDb, GetFrameTime());
        input.heardMidi = -1.0f;
        if (!isSounding(input.floor, input.levelDb)) continue; // a quiet instrument input counts as much as a loud mic
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
static void instrumentsSection(Settings& settings){
    const float s = menuScale();
    screen.shownFrame = (int)ImGui::GetFrameCount();
    // Whatever changes here which inputs are the instruments', the monitor hears the new ones (checked at the end)
    const int guitarBefore = settings.guitarChannel, bassBefore = settings.bassChannel, voiceBefore = settings.voiceChannel;
    const bool exclusiveBefore = settings.exclusiveInput;
    listenToInputs(settings);

    settingsGroup("INPUT");
    settingInfo("Input device", "Changed in Audio", settings.inputDevice.empty() ? "System default" : settings.inputDevice.c_str());
#ifdef _WIN32
    if (screen.listening && captureIsAsio()){
        // ASIO: straight to the interface. Its buffer size is the latency to play with, in the driver's own window.
        std::string latency = TextFormat("%.1f ms of input latency. A smaller buffer is faster; too small and it crackles.", captureLatencySeconds() * 1000.0);
        if (settingButton("ASIO driver", latency.c_str(), "Driver settings")) openInputDriverSettings();
    } else {
        // Windows' own effects on microphones (noise suppression) let an instrument through only while someone speaks
        const char* how = !screen.listening ? "Skips Windows' effects, which can cut an instrument"
                        : captureIsExclusive() ? "Windows' effects are skipped; other programs can't use this input meanwhile"
                        : settings.exclusiveInput ? "Another program has this input, so it's shared: Windows' effects may cut your instrument"
                        : "Shared: Windows' effects may cut your instrument";
        if (settingToggle("Keep the input to lahn alone", how, &settings.exclusiveInput)){
            setExclusiveCapture(settings.exclusiveInput);
            stopListening(); // opened again, the new way, next frame
        }
    }
#endif
    if (!screen.listening){
        settingNote(("Can't listen: " + screen.listenError).c_str(), UiColor::Bad);
        return;
    }
    // From a pluck to the screen: the input's buffering, the attack found (one 2.7 ms step), one frame
    double inputMs = captureLatencySeconds() * 1000.0, frameMs = GetFrameTime() * 1000.0;
    settingInfo("From pluck to screen", "Timing is judged from the pluck itself. Naming the note takes 10 to 60 ms more.",
                TextFormat("about %.0f ms", inputMs + 3.0 + frameMs));

    settingsGroup("WHAT EACH INPUT HEARS");
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const UiFonts& fonts = uiFonts();
    for (int c = 0; c < (int)screen.inputs.size(); c++){
        const auto& input = screen.inputs[c];
        std::string lowest = input.lowestHz > 0.0f
            ? TextFormat("Lowest note %s%d: %s", pitchClassName((int)std::lround(frequencyToMidi(input.lowestHz))),
                         pitchOctave((int)std::lround(frequencyToMidi(input.lowestHz))), guessInstrument(input.lowestHz).c_str())
            : "Play its lowest string";
        SettingControl row = settingRow(TextFormat("Input %d", c + 1), lowest.c_str(), 26 * s);
        // Its level (green while it's played) and the note it hears now
        float meterRight = row.max.x - 60 * s, middle = (row.min.y + row.max.y) / 2;
        float fill = std::clamp((input.levelDb + 60.0f) / 60.0f, 0.0f, 1.0f);
        draw->AddRectFilled(ImVec2(row.min.x, middle - 3 * s), ImVec2(meterRight, middle + 3 * s), uiColor(UiColor::StaffLine), 3 * s);
        if (fill > 0.02f){
            UiColor color = isSounding(input.floor, input.levelDb) ? UiColor::Good : UiColor::Dim;
            draw->AddRectFilled(ImVec2(row.min.x, middle - 3 * s), ImVec2(row.min.x + (meterRight - row.min.x) * fill, middle + 3 * s), uiColor(color), 3 * s);
        }
        std::string now = input.heardMidi >= 0.0f ? TextFormat("%s%d", pitchClassName((int)std::lround(input.heardMidi)), pitchOctave((int)std::lround(input.heardMidi))) : "-";
        float noteWidth = fonts.bold ? fonts.bold->CalcTextSizeA(17 * s, FLT_MAX, 0.0f, now.c_str()).x : 20 * s;
        draw->AddText(fonts.bold, 17 * s, ImVec2(row.max.x - noteWidth, middle - 9 * s), uiColor(input.heardMidi >= 0.0f ? UiColor::Accent : UiColor::Dim), now.c_str());
    }
    if (settingButton("Listen again", "Forget the lowest notes heard, to check an instrument again", "Listen again")){
        for (auto& input : screen.inputs) input.lowestHz = 0.0f;
    }

    // Detecting: the input that's clearly sounding, for half a second, while the player plays the instrument. Each
    // against its own floor, and the one risen most wins (core/inputs: playedInput), the inputs other instruments are
    // on counting less: sound bleeds, and a voice makes the bass's strings ring on its input too. An instrument that
    // was on the input found goes back to all inputs mixed: two never share one by mistake.
    if (screen.detecting >= 0){
        double now = GetTime();
        const int count = (int)screen.inputs.size();
        screen.loudSince.resize(count, -1.0);
        std::vector<float> rises(count, 0.0f);
        std::vector<bool> taken(count, false);
        for (int c = 0; c < count; c++){
            const auto& input = screen.inputs[c];
            bool loud = isSounding(input.floor, input.levelDb);
            if (!loud) screen.loudSince[c] = -1.0;
            else if (screen.loudSince[c] < 0.0) screen.loudSince[c] = now;
            if (loud) rises[c] = riseAboveFloor(input.floor, input.levelDb);
        }
        for (InputRole other : { InputRole::Guitar, InputRole::Bass, InputRole::Voice }){
            int channel = roleChannel(settings, other);
            if ((int)other != screen.detecting && channel >= 0 && channel < count) taken[channel] = true;
        }
        int played = playedInput(rises, taken);
        if (played >= 0 && now - screen.loudSince[played] > 0.5){
            int found = screen.inputs.size() > 1 ? played : -1;
            for (InputRole other : { InputRole::Guitar, InputRole::Bass, InputRole::Voice }){
                if ((int)other != screen.detecting && found >= 0 && roleChannel(settings, other) == found) roleChannel(settings, other) = -1;
            }
            roleChannel(settings, (InputRole)screen.detecting) = found;
            screen.detecting = -1;
        } else if (now - screen.detectStarted > 10.0){
            screen.detecting = -1; // nothing played: give up quietly
        }
    }

    // Each instrument's input: chosen, or found by playing it (Detect). A warning if what's heard there doesn't fit.
    settingsGroup("EACH INSTRUMENT'S INPUT");
    std::vector<std::string> choices = { "All inputs mixed" };
    for (int c = 0; c < (int)screen.inputs.size(); c++) choices.push_back(TextFormat("Input %d", c + 1));
    for (InputRole role : { InputRole::Guitar, InputRole::Bass, InputRole::Voice }){
        int& channel = roleChannel(settings, role);
        bool detectingThis = screen.detecting == (int)role;
        bool misfit = channel >= 0 && channel < (int)screen.inputs.size() && !fitsRole(role, screen.inputs[channel].lowestHz);
        std::string hint = detectingThis ? TextFormat("Play your %s...", inputRoleName(role))
                         : misfit ? TextFormat("This input sounds like %s", guessInstrument(screen.inputs[channel].lowestHz).c_str())
                         : "Judged from its own input, never mixed with the others";
        SettingControl row = settingRow(inputRoleName(role), hint.c_str(), settingsControlHeight());
        ImGui::PushID((int)role);
        const float buttonWidth = 90 * s;
        int chosen = channel + 1;
        if (settingsDropdownAt("input", row.min, ImVec2(row.max.x - buttonWidth - 8 * s, row.max.y), &chosen, choices)) channel = chosen - 1;
        if (settingsButtonAt("detect", ImVec2(row.max.x - buttonWidth, row.min.y), row.max, detectingThis ? "Listening" : "Detect") && !detectingThis){
            screen.detecting = (int)role;
            screen.detectStarted = GetTime();
            screen.loudSince.assign(screen.inputs.size(), -1.0);
        }
        ImGui::PopID();
    }
    if (settings.guitarChannel != guitarBefore || settings.bassChannel != bassBefore || settings.voiceChannel != voiceBefore ||
        settings.exclusiveInput != exclusiveBefore){
        applyMonitor(settings, screen.monitorError);
    }
}

static void gameplaySection(Settings& settings, SettingsChoice& choice){
    settingsGroup("PLAYING");
    int input = !settings.playWithInstrument ? 0 : settings.playInstrument == InputRole::Bass ? 2 : 1;
    if (settingSegments("Learn's exercises: play with", "Songs are played on each part's own instrument", &input, { "Keyboard", "Guitar", "Bass" })){
        settings.playWithInstrument = input != 0;
        if (input != 0) settings.playInstrument = input == 2 ? InputRole::Bass : InputRole::Guitar;
    }
    settingSlider("Note speed", "Faster spreads the notes further apart. Timing is judged the same.", &settings.noteSpeed, 100.0f, 1500.0f, "%.0f px/s");

    settingsGroup("LATENCY");
    settingInfo("Sound out", outputDeviceName(), TextFormat("%.0f ms", outputLatencySeconds() * 1000.0));
    settingSliderInt("Global offset", "How late sound reaches you (Bluetooth, slow drivers)", &settings.globalOffsetMs, -500, 500, "%d ms");
    if (settingButton("Measure it", "Tap along to clicks", "Tap along")) choice = SettingsChoice::CalibrateTapping;
    settingSliderInt("Input offset", "Your input device's own delay", &settings.inputOffsetMs, -500, 500, "%d ms");
    if (settingButton("Measure your instrument", "Play along to clicks. Measure by tapping first.", "Play along")) choice = SettingsChoice::CalibrateInstrument;
}

// The sections, a list on the left: Tab and Shift+Tab move through them, a click picks one
static void sectionList(float s){
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const UiFonts& fonts = uiFonts();
    const float left = ImGui::GetWindowWidth() * 0.07f, top = ImGui::GetWindowHeight() * 0.22f;
    const ImVec2 mouse = ImGui::GetMousePos();
    for (int i = 0; i < SECTION_COUNT; i++){
        const bool on = (int)screen.section == i;
        const float y = top + i * SECTION_ROW * s;
        const float width = fonts.bold ? fonts.bold->CalcTextSizeA(21 * s, FLT_MAX, 0.0f, SECTION_NAMES[i]).x : 100 * s;
        const bool hovered = mouse.x >= left - 16 * s && mouse.x <= left + width + 16 * s && mouse.y >= y - 6 * s && mouse.y <= y + 30 * s;
        if (on) draw->AddRectFilled(ImVec2(left - 16 * s, y + 3 * s), ImVec2(left - 13 * s, y + 25 * s), uiColor(UiColor::Accent), 1.5f * s);
        draw->AddText(fonts.bold, 21 * s, ImVec2(left, y), uiColor(on ? UiColor::Ink : hovered ? UiColor::Ink : UiColor::Dim, on || hovered ? 1.0f : 0.9f),
                      SECTION_NAMES[i]);
        if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) screen.section = (Section)i;
    }
    // Tab is the sections' own, not ImGui's (which would move between controls)
    const ImGuiID owner = ImGui::GetID("SettingsSections");
    ImGui::SetKeyOwner(ImGuiKey_Tab, owner);
    if (ImGui::IsKeyPressed(ImGuiKey_Tab, ImGuiInputFlags_Repeat, owner)){
        int step = ImGui::GetIO().KeyShift ? SECTION_COUNT - 1 : 1;
        screen.section = (Section)(((int)screen.section + step) % SECTION_COUNT);
    }
}

SettingsChoice settingsScreen(Settings& settings, const std::string& soundsDir, const std::string& error){
    SettingsChoice choice = SettingsChoice::None;
    beginMenu("Settings");
    const float s = menuScale(), width = ImGui::GetWindowWidth(), height = ImGui::GetWindowHeight();
    menuScreenTitle("Settings", s);
    sectionList(s);

    // The chosen section, on a card that scrolls
    const float cardLeft = width * 0.30f, cardTop = height * 0.17f, cardRight = width * 0.93f, cardBottom = height - 64 * s;
    ImGui::SetCursorScreenPos(ImVec2(cardLeft, cardTop));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, uiColorVec(UiColor::Card));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 14 * s);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(32 * s, 10 * s));
    ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize, 6 * s);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
    // NavFlattened: Up and Down go straight to the card's controls, not to the card as a whole first
    // One card per section, so each keeps its own scroll position
    ImGui::BeginChild(TextFormat("SettingsCard%d", (int)screen.section), ImVec2(cardRight - cardLeft, cardBottom - cardTop),
                      ImGuiChildFlags_AlwaysUseWindowPadding | ImGuiChildFlags_NavFlattened);
    if (!screen.status.empty()) settingNote(screen.status.c_str(), screen.statusIsError ? UiColor::Bad : UiColor::Dim);
    if (!error.empty()) settingNote(error.c_str(), UiColor::Bad);
    switch (screen.section){
        case Section::Audio:       audioSection(settings, soundsDir, choice); break;
        case Section::Instruments: instrumentsSection(settings); break;
        case Section::Gameplay:    gameplaySection(settings, choice); break;
        case Section::Display:     displaySection(settings); break;
        case Section::PianoKeys:   pianoKeysSection(settings); break;
    }
    ImGui::Dummy(ImVec2(0, 16 * s));
    ImGui::EndChild();
    ImGui::PopStyleVar(4);
    ImGui::PopStyleColor();
    if (screen.listening && screen.shownFrame != (int)ImGui::GetFrameCount()) stopListening(); // left the Instruments section

    menuScreenHint("Tab  section    Up/Down  setting    Left/Right  adjust    Enter  choose    Esc  back", s);
    screen.popupWasOpen = ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId);
    ImGui::End();
    return choice;
}
