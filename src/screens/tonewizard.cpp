#include "screens/tonewizard.h"

#include "audio/audio.h"
#include "audio/capture.h"
#include "core/cabinets.h"
#include "core/tonelibrary.h"
#include "imgui.h"
#include "raylib.h"
#include "screens/settingsscreen.h"
#include "ui/menulist.h"
#include "ui/settingsui.h"
#include "ui/theme.h"
#include "ui/ui.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <filesystem>

// Sizes at a 720-pixel-tall window
const float CARD_WIDTH = 196.0f;
const float CARD_HEIGHT = 356.0f;
const float CARD_GAP = 30.0f;        // between two cards: room for the arrow the sound takes
const float KNOB_RADIUS = 21.0f;
const float KNOB_CELL = 82.0f;       // a knob, its name and its value, one below the other
const float DRAG_RANGE = 180.0f;     // pixels dragged for a knob's whole range (Shift: five times finer)
const double SAVE_AFTER_S = 0.4;     // a tone saves itself this long after the last change, once no knob is held

static struct {
    std::string folder;
    std::vector<Tone> userTones;
    std::vector<std::string> problems; // tone files that couldn't be read
    // The wizard
    Tone editing;             // the tone on the board, as it's being changed
    bool builtIn = false;     // one of lahn's: the first change makes it the player's own copy
    bool dirty = false;       // changed since it was saved
    double changedAt = 0.0;
    std::string nameField;    // the name as it's typed
    float scroll = 0.0f;      // the board, scrolled sideways
    std::string status;
    bool statusBad = false;
    int hovered = -1;         // the card the mouse is over, for its description
    const ParameterInfo* hoveredKnob = nullptr; // the knob under the mouse (or turned): explained instead
} tones;

// --- The library ----------------------------------------------------------------------------------------------

void initTones(const std::string& tonesFolder){
    tones.folder = tonesFolder;
    tones.problems.clear();
    tones.userTones = loadUserTones(tonesFolder, tones.problems);
}

// The player's impulse responses (.wav) and captures (.nam), beside their tones: what a cabinet or a capture with a
// file plays
static std::string cabinetsFolder(){
    return (std::filesystem::path(tones.folder).parent_path() / "cabinets").string();
}
static std::string capturesFolder(){
    return (std::filesystem::path(tones.folder).parent_path() / "captures").string();
}

static Tone* userTone(const std::string& name){
    for (Tone& tone : tones.userTones) if (tone.name == name) return &tone;
    return nullptr;
}

Tone toneNamed(const std::string& name){
    return findTone(name, tones.userTones);
}

std::vector<std::string> toneNames(){
    std::vector<std::string> names;
    for (const Tone& tone : builtInTones()) names.push_back(tone.name);
    for (const Tone& tone : tones.userTones) names.push_back(tone.name);
    return names;
}

static void playTone(const Tone& tone, float volume){
    ToneParameters parameters = toneParameters(tone);
    attachCabinets(parameters, cabinetsFolder()); // their impulse responses and models, made ready here rather than on the
    attachCaptures(parameters, capturesFolder()); // audio thread
    parameters.volume *= volume;
    setMonitorTone(parameters);
}

void hearInstrument(Settings& settings, InputRole instrument){
    if (instrument != InputRole::Guitar && instrument != InputRole::Bass) return;
    settings.heardInstrument = instrument;
    applyTone(settings);
}

void applyTone(const Settings& settings){
    playTone(toneNamed(settings.toneFor(settings.heardInstrument)), settings.monitorVolume);
}

// --- Editing ------------------------------------------------------------------------------------------------

static void setStatus(const std::string& text, bool bad){
    tones.status = text;
    tones.statusBad = bad;
}

static void save(){
    if (!tones.dirty || tones.builtIn) return;
    std::string error;
    if (saveUserTone(tones.folder, tones.editing, error)){
        if (Tone* kept = userTone(tones.editing.name)) *kept = tones.editing;
        tones.dirty = false;
    } else {
        setStatus(error, true);
    }
}

static void show(Settings& settings, const Tone& tone){
    save();
    tones.editing = tone;
    tones.builtIn = userTone(tone.name) == nullptr;
    tones.nameField = tone.name;
    tones.dirty = false;
    tones.scroll = 0.0f;
    settings.toneFor(settings.heardInstrument) = tone.name;
    applyTone(settings);
}

// Something on the board changed: heard at once, saved a moment later. A built-in tone becomes the player's own copy.
static void changed(Settings& settings){
    if (tones.builtIn){
        tones.editing.name = freeToneName("My " + tones.editing.name, tones.userTones);
        tones.userTones.push_back(tones.editing);
        tones.builtIn = false;
        tones.nameField = tones.editing.name;
        settings.toneFor(settings.heardInstrument) = tones.editing.name;
        setStatus("Saved as your own tone: " + tones.editing.name, false);
    }
    tones.dirty = true;
    tones.changedAt = GetTime();
    playTone(tones.editing, settings.monitorVolume);
}

void openToneWizard(Settings& settings){
    initTones(tones.folder); // files may have been added while away
    show(settings, toneNamed(settings.toneFor(settings.heardInstrument)));
    setStatus(tones.problems.empty() ? "" : "Couldn't read " + tones.problems.front(), !tones.problems.empty());
}

void closeToneWizard(Settings& settings){
    save();
    applyTone(settings);
}

// An impulse response (a cabinet) or a capture dropped on the window: kept in its folder, and played by the effect of
// its kind under the mouse, else the tone's first, else a new one (a capture goes in before the cabinet, as an amp is)
static void importEffectFile(Settings& settings, const std::string& path, EffectType type){
    namespace fs = std::filesystem;
    const bool capture = type == EffectType::Capture;
    const std::string folder = capture ? capturesFolder() : cabinetsFolder();
    const std::string name = fs::u8path(path).filename().u8string();
    std::error_code ec;
    fs::create_directories(fs::u8path(folder), ec);
    const fs::path kept = fs::u8path(folder) / fs::u8path(name);
    if (!fs::equivalent(fs::u8path(path), kept, ec)) fs::copy_file(fs::u8path(path), kept, fs::copy_options::overwrite_existing, ec);
    if (ec){
        setStatus("Couldn't keep " + name + ": " + ec.message(), true);
        return;
    }
    std::vector<Effect>& effects = tones.editing.effects;
    int target = tones.hovered >= 0 && tones.hovered < (int)effects.size() && effects[tones.hovered].type == type ? tones.hovered : -1;
    for (int i = 0; i < (int)effects.size() && target < 0; i++) if (effects[i].type == type) target = i;
    std::string error;
    const bool playable = capture ? captureFromFile(kept.u8string(), target >= 0 ? target : 0, error) != nullptr
                                  : cabinetFromFile(kept.u8string(), error) != nullptr;
    if (!playable){
        setStatus(error, true);
        return;
    }
    if (target < 0){
        if ((int)effects.size() >= MAX_EFFECTS){
            setStatus(std::string("No room for a ") + (capture ? "capture" : "cabinet") + ": take an effect out first", true);
            return;
        }
        int at = (int)effects.size();
        if (capture) for (int i = (int)effects.size() - 1; i >= 0; i--) if (effects[i].type == EffectType::Cabinet) at = i;
        effects.insert(effects.begin() + at, makeEffect(type));
        target = at;
    }
    setEffectFile(effects[target], name);
    changed(settings);
    setStatus(std::string(capture ? "The capture plays " : "The cabinet plays ") + name, false);
}

// Files dropped on the window: tones added to the player's (the last one shown), impulse responses to a cabinet
static void importDropped(Settings& settings){
    if (!IsFileDropped()) return;
    FilePathList dropped = LoadDroppedFiles();
    for (unsigned i = 0; i < dropped.count; i++){
        std::string path = dropped.paths[i], error;
        Tone imported;
        std::string extension = std::filesystem::path(path).extension().string();
        for (char& c : extension) c = (char)std::tolower((unsigned char)c);
        if (extension == ".wav"){
            importEffectFile(settings, path, EffectType::Cabinet);
        } else if (extension == ".nam"){
            importEffectFile(settings, path, EffectType::Capture);
        } else if (extension != TONE_FILE_EXTENSION){
            setStatus(std::filesystem::path(path).filename().string() + " isn't a tone (.tone), an impulse response (.wav) or a capture (.nam)", true);
        } else if (importTone(path, tones.folder, tones.userTones, imported, error)){
            tones.userTones.push_back(imported);
            show(settings, imported);
            setStatus("Added: " + imported.name, false);
        } else {
            setStatus(error, true);
        }
    }
    UnloadDroppedFiles(dropped);
}

// --- Drawing ------------------------------------------------------------------------------------------------

static std::string valueText(const ParameterInfo& info, float value){
    if (info.choices) return info.choices[std::clamp((int)std::lround(value), (int)info.min, (int)info.max)];
    std::string unit = info.unit;
    if (unit == "%") return TextFormat("%.0f%%", value * 100.0f);
    if (unit == "dB") return TextFormat("%+.1f dB", value);
    if (unit == ":1") return TextFormat("%.1f:1", value);
    if (unit == "Hz") return value < 10.0f ? TextFormat("%.1f Hz", value) : TextFormat("%.0f Hz", value);
    if (unit == "ms") return value < 10.0f ? TextFormat("%.1f ms", value) : TextFormat("%.0f ms", value);
    return TextFormat("%.2f", value);
}

// A knob: dragged up or down (Shift for fine), the wheel nudges it, a double click puts it back. Its arc starts at
// the bottom left and turns clockwise to the bottom right; one that goes both ways (-12 to +12 dB) fills from the top.
static bool knob(const char* id, const ParameterInfo& info, float* value, ImVec2 center, float s, float alpha, const char* shown = nullptr){
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const UiFonts& fonts = uiFonts();
    const float radius = KNOB_RADIUS * s;
    ImGui::SetCursorScreenPos(ImVec2(center.x - radius, center.y - radius));
    ImGui::InvisibleButton(id, ImVec2(2 * radius, 2 * radius));
    bool hovered = ImGui::IsItemHovered(), active = ImGui::IsItemActive();
    if (hovered || active) tones.hoveredKnob = &info;
    ImGuiIO& io = ImGui::GetIO();
    const float range = info.max - info.min;
    float before = *value;
    if (active && io.MouseDelta.y != 0.0f) *value -= io.MouseDelta.y / (DRAG_RANGE * s) * range * (io.KeyShift ? 0.2f : 1.0f);
    if (hovered && io.MouseWheel != 0.0f) *value = info.choices ? std::round(*value) + (io.MouseWheel > 0.0f ? 1.0f : -1.0f) // the next choice
                                                                : *value + io.MouseWheel * range * 0.02f;
    if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) *value = info.standard;
    *value = std::clamp(*value, info.min, info.max);
    if (hovered || active) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);

    const float start = PI * 0.75f, sweep = PI * 1.5f; // from the bottom left, clockwise, to the bottom right
    float share = (*value - info.min) / range;
    bool bipolar = info.min < 0.0f && info.max > 0.0f;
    float from = bipolar ? start + sweep * (-info.min / range) : start;
    float to = start + sweep * share;
    float track = 3.5f * s;
    draw->PathArcTo(center, radius - track / 2, start, start + sweep, 40);
    draw->PathStroke(uiColor(UiColor::StaffLine, alpha), 0, track);
    if (std::fabs(to - from) > 0.01f){
        draw->PathArcTo(center, radius - track / 2, std::min(from, to), std::max(from, to), 40);
        draw->PathStroke(uiColor(UiColor::Accent, alpha), 0, track);
    }
    draw->AddCircleFilled(center, radius - track - 3 * s, uiColor(active || hovered ? UiColor::Ink : UiColor::Dim, (active ? 0.16f : 0.1f) * alpha), 32);
    ImVec2 tip(center.x + std::cos(to) * (radius - track - 6 * s), center.y + std::sin(to) * (radius - track - 6 * s));
    draw->AddLine(ImVec2(center.x + std::cos(to) * 4 * s, center.y + std::sin(to) * 4 * s), tip, uiColor(UiColor::Ink, alpha), 2.5f * s);

    auto centered = [&](ImFont* font, float size, float y, ImU32 color, const char* text){
        float width = font ? font->CalcTextSizeA(size, FLT_MAX, 0.0f, text).x : 0.0f;
        draw->AddText(font, size, ImVec2(center.x - width / 2, y), color, text);
    };
    centered(fonts.bold, 13 * s, center.y + radius + 6 * s, uiColor(UiColor::Ink, alpha), info.name);
    centered(fonts.mono, 11 * s, center.y + radius + 23 * s, uiColor(active ? UiColor::Accent : UiColor::Dim, alpha),
             shown ? shown : valueText(info, *value).c_str());
    return *value != before;
}

// A small switch, on or off
static bool effectSwitch(const char* id, bool* on, ImVec2 at, float s){
    ImDrawList* draw = ImGui::GetWindowDrawList();
    ImVec2 size(34 * s, 20 * s);
    ImGui::SetCursorScreenPos(at);
    bool clicked = ImGui::InvisibleButton(id, size);
    if (clicked) *on = !*on;
    if (ImGui::IsItemHovered()) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    draw->AddRectFilled(at, ImVec2(at.x + size.x, at.y + size.y), uiColor(*on ? UiColor::Accent : UiColor::StaffLine), size.y / 2);
    float knobX = *on ? at.x + size.x - size.y / 2 : at.x + size.y / 2;
    draw->AddCircleFilled(ImVec2(knobX, at.y + size.y / 2), size.y / 2 - 3 * s, uiColor(UiColor::Card), 20);
    return clicked;
}

// A little icon button on a card: its glyph drawn in the middle
enum class Icon { Left, Right, Remove };
static bool iconButton(const char* id, Icon icon, ImVec2 at, float s, bool enabled){
    ImDrawList* draw = ImGui::GetWindowDrawList();
    ImVec2 size(26 * s, 26 * s);
    ImGui::SetCursorScreenPos(at);
    bool clicked = ImGui::InvisibleButton(id, size) && enabled;
    bool hovered = ImGui::IsItemHovered() && enabled;
    if (hovered) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    if (hovered) draw->AddRectFilled(at, ImVec2(at.x + size.x, at.y + size.y), uiColor(UiColor::Ink, 0.07f), 6 * s);
    ImU32 color = uiColor(icon == Icon::Remove && hovered ? UiColor::Bad : UiColor::Ink, enabled ? (hovered ? 1.0f : 0.55f) : 0.18f);
    ImVec2 c(at.x + size.x / 2, at.y + size.y / 2);
    float r = 5 * s, w = 2.0f * s;
    if (icon == Icon::Remove){
        draw->AddLine(ImVec2(c.x - r, c.y - r), ImVec2(c.x + r, c.y + r), color, w);
        draw->AddLine(ImVec2(c.x - r, c.y + r), ImVec2(c.x + r, c.y - r), color, w);
    } else {
        float d = icon == Icon::Left ? -1.0f : 1.0f;
        ImVec2 points[3] = { ImVec2(c.x - d * r * 0.5f, c.y - r), ImVec2(c.x + d * r * 0.5f, c.y), ImVec2(c.x - d * r * 0.5f, c.y + r) };
        draw->AddPolyline(points, 3, color, ImDrawFlags_None, w);
    }
    return clicked;
}

// The arrow the sound takes from one card to the next
static void flowArrow(ImVec2 from, ImVec2 to, float s){
    ImDrawList* draw = ImGui::GetWindowDrawList();
    ImU32 color = uiColor(UiColor::Dim, 0.6f);
    draw->AddLine(from, ImVec2(to.x - 3 * s, to.y), color, 2.0f * s);
    ImVec2 head[3] = { ImVec2(to.x - 7 * s, to.y - 5 * s), ImVec2(to.x, to.y), ImVec2(to.x - 7 * s, to.y + 5 * s) };
    draw->AddPolyline(head, 3, color, ImDrawFlags_None, 2.0f * s);
}

void toneWizardScreen(Settings& settings){
    importDropped(settings);
    beginMenu("Tone wizard");
    const float s = menuScale(), width = ImGui::GetWindowWidth(), height = ImGui::GetWindowHeight();
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const UiFonts& fonts = uiFonts();
    const float left = width * 0.07f, right = width * 0.93f;
    menuScreenTitle("Tone wizard", s);
    // Whose tone: the bass's or the guitar's, each its own. The one shown is the one heard.
    {
        const InputRole roles[2] = { InputRole::Bass, InputRole::Guitar };
        const char* names[2] = { "BASS", "GUITAR" };
        float x = width * 0.55f, y = height * 0.09f + 14 * s;
        draw->AddText(fonts.mono, 13 * s, ImVec2(x, y + 4 * s), uiColor(UiColor::Dim), "TONE FOR");
        x += 100 * s;
        for (int i = 0; i < 2; i++){
            bool on = settings.heardInstrument == roles[i];
            ImVec2 size = fonts.bold ? fonts.bold->CalcTextSizeA(20 * s, FLT_MAX, 0.0f, names[i]) : ImVec2(60 * s, 20 * s);
            ImGui::SetCursorScreenPos(ImVec2(x, y));
            bool clicked = ImGui::InvisibleButton(names[i], size);
            bool hovered = ImGui::IsItemHovered();
            if (hovered && !on) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            draw->AddText(fonts.bold, 20 * s, ImVec2(x, y), uiColor(on || hovered ? UiColor::Ink : UiColor::Dim), names[i]);
            if (on) draw->AddRectFilled(ImVec2(x, y + size.y + 3 * s), ImVec2(x + size.x, y + size.y + 5 * s), uiColor(UiColor::Accent));
            if (clicked && !on){
                save();
                settings.heardInstrument = roles[i];
                show(settings, toneNamed(settings.toneFor(roles[i])));
                setStatus("", false);
            }
            x += size.x + 22 * s;
        }
    }
    draw->AddText(fonts.text, 18 * s, ImVec2(left, height * 0.09f + 52 * s), uiColor(UiColor::Dim),
                  "Your sound goes through these, left to right. Play while you turn the knobs.");

    // The tone: which one, its name, and what to do with it
    const float rowY = height * 0.09f + 94 * s, controlHeight = settingsControlHeight();
    std::vector<std::string> names = toneNames();
    int chosen = 0;
    for (int i = 0; i < (int)names.size(); i++) if (names[i] == tones.editing.name) chosen = i;
    if (settingsDropdownAt("tone", ImVec2(left, rowY), ImVec2(left + 220 * s, rowY + controlHeight), &chosen, names)){
        show(settings, toneNamed(names[chosen]));
        setStatus("", false);
    }
    // Its name, typed: a built-in tone renamed becomes the player's copy under that name
    static char nameBuffer[128];
    static bool typingName = false;
    if (!typingName && tones.nameField != nameBuffer) std::snprintf(nameBuffer, sizeof nameBuffer, "%s", tones.nameField.c_str());
    // In the settings' look: the controls' height, bold text, a quiet border that lights up while typing
    ImGui::PushFont(fonts.bold, 15 * s);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(12 * s, (controlHeight - 15 * s) / 2));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 8 * s);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, uiColorVec(UiColor::Card));
    ImGui::PushStyleColor(ImGuiCol_Border, uiColorVec(typingName ? UiColor::Accent : UiColor::StaffLine));
    ImGui::SetCursorScreenPos(ImVec2(left + 236 * s, rowY));
    ImGui::SetNextItemWidth(220 * s);
    bool renamed = ImGui::InputText("##toneName", nameBuffer, sizeof nameBuffer, ImGuiInputTextFlags_EnterReturnsTrue);
    renamed |= ImGui::IsItemDeactivatedAfterEdit();
    typingName = ImGui::IsItemActive();
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(3);
    ImGui::PopFont();
    if (renamed){
        std::string wanted = nameBuffer;
        if (!wanted.empty() && wanted != tones.editing.name){
            std::string oldName = tones.editing.name;
            bool wasBuiltIn = tones.builtIn;
            std::vector<Tone> others;
            for (const Tone& tone : tones.userTones) if (tone.name != oldName) others.push_back(tone);
            tones.editing.name = freeToneName(wanted, others);
            std::string error;
            if (!wasBuiltIn){
                deleteUserTone(tones.folder, oldName, error);
                tones.userTones = others;
            }
            tones.userTones.push_back(tones.editing);
            tones.builtIn = false;
            tones.dirty = true;
            save();
            settings.toneFor(settings.heardInstrument) = tones.editing.name;
            setStatus(wasBuiltIn ? "Saved as your own tone: " + tones.editing.name : "Renamed", false);
        }
        tones.nameField = tones.editing.name;
        std::snprintf(nameBuffer, sizeof nameBuffer, "%s", tones.nameField.c_str());
    }
    float buttonX = left + 472 * s;
    auto button = [&](const char* id, const char* text, float buttonWidth){
        bool pressed = settingsButtonAt(id, ImVec2(buttonX, rowY), ImVec2(buttonX + buttonWidth * s, rowY + controlHeight), text);
        buttonX += buttonWidth * s + 10 * s;
        return pressed;
    };
    if (button("new", "New", 70)){
        Tone fresh{ freeToneName("My tone", tones.userTones), { makeEffect(EffectType::Compressor), makeEffect(EffectType::Amp) }, 0.8f };
        tones.userTones.push_back(fresh);
        show(settings, fresh);
        tones.dirty = true;
        save();
        setStatus("A new tone: add effects with +", false);
    }
    if (button("copy", "Copy", 70)){
        Tone copy = tones.editing;
        copy.name = freeToneName(tones.editing.name, tones.userTones);
        tones.userTones.push_back(copy);
        show(settings, copy);
        tones.dirty = true;
        save();
        setStatus("Copied: " + copy.name, false);
    }
    ImGui::BeginDisabled(tones.builtIn);
    if (button("delete", "Delete", 80)){
        std::string error, name = tones.editing.name;
        if (deleteUserTone(tones.folder, name, error)){
            tones.userTones.erase(std::remove_if(tones.userTones.begin(), tones.userTones.end(), [&](const Tone& t){ return t.name == name; }), tones.userTones.end());
            tones.dirty = false;
            show(settings, builtInTones().front());
            setStatus("Deleted " + name, false);
        } else {
            setStatus(error, true);
        }
    }
    ImGui::EndDisabled();
    if (button("folder", "Share", 80)){
        save();
        openFolder(tones.folder);
        setStatus("Your tones are the .tone files there: send them to anyone. Drop one on this window to add it.", false);
    }

    // Not heard: the tone only shapes the instrument's own sound
    float noteY = rowY + controlHeight + 14 * s;
    if (!settings.monitorOn || settings.monitorSynth){
        const char* notHeard = "You're not hearing your own sound now: the tone shapes it, not the synth.";
        draw->AddText(fonts.text, 16 * s, ImVec2(left, noteY), uiColor(UiColor::Accent), notHeard);
        float after = left + (fonts.text ? fonts.text->CalcTextSizeA(16 * s, FLT_MAX, 0.0f, notHeard).x : 400 * s) + 16 * s;
        float buttonY = noteY + 8 * s - controlHeight / 2;
        if (settingsButtonAt("hear", ImVec2(after, buttonY), ImVec2(after + 110 * s, buttonY + controlHeight), "Hear it")){
            settings.monitorOn = true;
            settings.monitorSynth = false;
            std::string error;
            applyMonitor(settings, error);
            if (!error.empty()) setStatus(error, true);
        }
    } else if (!tones.status.empty()){
        draw->AddText(fonts.text, 16 * s, ImVec2(left, noteY), uiColor(tones.statusBad ? UiColor::Bad : UiColor::Good), tones.status.c_str());
    }

    // The board: the instrument in, each effect a card, the tone's volume out. Scrolled sideways when it's wide.
    const float boardTop = height * 0.36f, cardWidth = CARD_WIDTH * s, cardHeight = CARD_HEIGHT * s, gap = CARD_GAP * s;
    const int count = (int)tones.editing.effects.size();
    const float endWidth = 124 * s;
    float contentWidth = endWidth + gap + count * (cardWidth + gap) + (count < MAX_EFFECTS ? cardWidth * 0.6f + gap : 0.0f) + endWidth;
    float maxScroll = std::max(0.0f, contentWidth - (right - left));
    ImVec2 mouse = ImGui::GetMousePos();
    bool overBoard = mouse.y >= boardTop && mouse.y <= boardTop + cardHeight && mouse.x >= left && mouse.x <= right;
    if (overBoard && !ImGui::IsAnyItemHovered()) tones.scroll -= ImGui::GetIO().MouseWheel * 60 * s;
    tones.scroll = std::clamp(tones.scroll, 0.0f, maxScroll);
    draw->PushClipRect(ImVec2(left - 4 * s, boardTop - 10 * s), ImVec2(right + 4 * s, boardTop + cardHeight + 10 * s), true);
    float x = left - tones.scroll;
    const float middleY = boardTop + cardHeight / 2;

    // In: the instrument
    auto endCap = [&](const char* label, const char* detail, float at){
        draw->AddText(fonts.mono, 13 * s, ImVec2(at, middleY - 22 * s), uiColor(UiColor::Dim), label);
        draw->AddText(fonts.bold, 16 * s, ImVec2(at, middleY - 4 * s), uiColor(UiColor::Ink), detail);
    };
    endCap("IN", "Your instrument", x);
    x += endWidth;
    flowArrow(ImVec2(x, middleY), ImVec2(x + gap - 4 * s, middleY), s);
    x += gap;

    // The effects
    enum class Action { None, Left, Right, Remove } action = Action::None;
    int actionAt = -1;
    bool edited = false;
    tones.hovered = -1;
    tones.hoveredKnob = nullptr;
    for (int i = 0; i < count; i++){
        Effect& effect = tones.editing.effects[i];
        const EffectInfo& info = effectInfo(effect.type);
        ImGui::PushID(i);
        ImVec2 min(x, boardTop), max(x + cardWidth, boardTop + cardHeight);
        if (mouse.x >= min.x && mouse.x < max.x && mouse.y >= min.y && mouse.y < max.y) tones.hovered = i;
        const float alpha = effect.on ? 1.0f : 0.45f;
        draw->AddRectFilled(ImVec2(min.x, min.y + 3 * s), ImVec2(max.x, max.y + 3 * s), uiColor(UiColor::Ink, 0.05f), 12 * s);
        draw->AddRectFilled(min, max, uiColor(UiColor::Card), 12 * s);
        draw->AddRectFilled(min, ImVec2(max.x, min.y + 5 * s), uiColor(effect.on ? UiColor::Accent : UiColor::StaffLine), 12 * s, ImDrawFlags_RoundCornersTop);
        draw->AddText(fonts.bold, 19 * s, ImVec2(min.x + 16 * s, min.y + 20 * s), uiColor(UiColor::Ink, alpha), info.name);
        if (effectSwitch("on", &effect.on, ImVec2(max.x - 50 * s, min.y + 20 * s), s)) edited = true;
        // A capture: the file it plays, or what to do for one
        if (effect.type == EffectType::Capture){
            std::string line = "Drop a .nam file here", error;
            UiColor color = UiColor::Dim;
            if (effect.file[0]){
                const std::string path = (std::filesystem::u8path(capturesFolder()) / std::filesystem::u8path(effect.file)).u8string();
                line = std::filesystem::u8path(effect.file).stem().u8string();
                if (!captureFromFile(path, i, error)){
                    line = "Can't play " + line;
                    color = UiColor::Bad;
                }
                if (line.size() > 26) line = line.substr(0, 25) + "...";
            }
            draw->AddText(fonts.text, 13 * s, ImVec2(min.x + 16 * s, min.y + 46 * s), uiColor(color, alpha), line.c_str());
        }
        // Its knobs, two to a row
        for (int p = 0; p < info.parameterCount; p++){
            int column = p % 2, row = p / 2;
            ImVec2 center(min.x + cardWidth * (column == 0 ? 0.28f : 0.72f), min.y + 92 * s + row * KNOB_CELL * s);
            if (info.parameterCount % 2 == 1 && p == info.parameterCount - 1) center.x = min.x + cardWidth / 2; // the odd one out, centered
            ImGui::PushID(p);
            // A cabinet playing a file of the player's says so on its speaker; turning it goes back to the built-in ones
            std::string file;
            if (effect.type == EffectType::Cabinet && p == 0 && effect.file[0]){
                file = std::filesystem::path(effect.file).stem().string();
                if (file.size() > 22) file = file.substr(0, 21) + "...";
            }
            if (knob("knob", info.parameters[p], &effect.values[p], center, s, alpha, file.empty() ? nullptr : file.c_str())){
                edited = true;
                if (!file.empty()) effect.file[0] = '\0';
            }
            ImGui::PopID();
        }
        // Along its foot: move it earlier or later in the chain, or take it out
        float footY = max.y - 36 * s;
        if (iconButton("left", Icon::Left, ImVec2(min.x + 10 * s, footY), s, i > 0)){ action = Action::Left; actionAt = i; }
        if (iconButton("right", Icon::Right, ImVec2(min.x + 40 * s, footY), s, i + 1 < count)){ action = Action::Right; actionAt = i; }
        if (iconButton("remove", Icon::Remove, ImVec2(max.x - 36 * s, footY), s, true)){ action = Action::Remove; actionAt = i; }
        ImGui::PopID();
        x += cardWidth;
        flowArrow(ImVec2(x + 4 * s, middleY), ImVec2(x + gap - 4 * s, middleY), s);
        x += gap;
    }

    // Adding one: a card of its own, listing the effects beside it
    const float menuWidth = 380 * s;
    ImVec2 menuAt(left, boardTop);
    ImVec2 menuPivot(0.0f, 0.0f);
    if (count < MAX_EFFECTS){
        ImVec2 min(x, boardTop + cardHeight * 0.3f), max(x + cardWidth * 0.6f, boardTop + cardHeight * 0.7f);
        ImGui::SetCursorScreenPos(min);
        bool add = ImGui::InvisibleButton("add", ImVec2(max.x - min.x, max.y - min.y));
        bool hovered = ImGui::IsItemHovered();
        if (hovered) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        draw->AddRect(min, max, uiColor(hovered ? UiColor::Accent : UiColor::Dim, hovered ? 1.0f : 0.5f), 12 * s, 0, 1.5f * s);
        ImVec2 c((min.x + max.x) / 2, (min.y + max.y) / 2 - 10 * s);
        draw->AddLine(ImVec2(c.x - 10 * s, c.y), ImVec2(c.x + 10 * s, c.y), uiColor(hovered ? UiColor::Accent : UiColor::Ink), 2.5f * s);
        draw->AddLine(ImVec2(c.x, c.y - 10 * s), ImVec2(c.x, c.y + 10 * s), uiColor(hovered ? UiColor::Accent : UiColor::Ink), 2.5f * s);
        const char* text = "Add effect";
        float textWidth = fonts.bold ? fonts.bold->CalcTextSizeA(14 * s, FLT_MAX, 0.0f, text).x : 60 * s;
        draw->AddText(fonts.bold, 14 * s, ImVec2(c.x - textWidth / 2, c.y + 20 * s), uiColor(UiColor::Ink, 0.8f), text);
        if (add) ImGui::OpenPopup("add effect");
        // Right of it if there's room, else left of it
        bool roomRight = max.x + 12 * s + menuWidth < width;
        menuAt = ImVec2(roomRight ? max.x + 12 * s : min.x - 12 * s, height - 16 * s); // its foot near the screen's
        menuPivot = ImVec2(roomRight ? 0.0f : 1.0f, 1.0f);
        x = max.x;
        flowArrow(ImVec2(x + 4 * s, middleY), ImVec2(x + gap - 4 * s, middleY), s);
        x += gap;
    }
    ImGui::SetNextWindowPos(menuAt, ImGuiCond_Always, menuPivot);
    ImGui::SetNextWindowSize(ImVec2(menuWidth, 0));
    if (ImGui::BeginPopup("add effect")){
        for (int type = 0; type < (int)EffectType::Count; type++){
            const EffectInfo& info = effectInfo((EffectType)type);
            ImGui::PushID(type);
            ImVec2 at = ImGui::GetCursorScreenPos();
            if (ImGui::Selectable("##effect", false, 0, ImVec2(0, 52 * s))){
                tones.editing.effects.push_back(makeEffect((EffectType)type));
                edited = true;
                tones.scroll = 1e9f; // to the end, where it went
            }
            ImGui::GetWindowDrawList()->AddText(fonts.bold, 16 * s, ImVec2(at.x + 6 * s, at.y + 4 * s), uiColor(UiColor::Ink), info.name);
            ImGui::GetWindowDrawList()->AddText(fonts.text, 13 * s, ImVec2(at.x + 6 * s, at.y + 24 * s), uiColor(UiColor::Dim), info.description,
                                                nullptr, menuWidth - 36 * s);
            ImGui::PopID();
        }
        ImGui::EndPopup();
    }

    // Out: the tone's volume, as a knob
    endCap("OUT", "", x);
    static const ParameterInfo VOLUME = { "volume", "Volume", 0.0f, 1.0f, 0.8f, "%",
                                          "How loud the whole tone is, after every effect" };
    if (knob("volume", VOLUME, &tones.editing.volume, ImVec2(x + 26 * s, middleY + 30 * s), s, 1.0f)) edited = true;
    draw->PopClipRect();

    // The scroll bar, when the board is wider than the screen: its thumb dragged, or the track clicked to jump
    if (maxScroll > 8 * s){
        const float trackY = boardTop + cardHeight + 16 * s, trackWidth = right - left, thickness = 6 * s;
        const float view = right - left, thumbWidth = std::max(40 * s, trackWidth * view / contentWidth);
        float thumbX = left + (trackWidth - thumbWidth) * tones.scroll / maxScroll;
        ImGui::SetCursorScreenPos(ImVec2(left, trackY - 6 * s));
        ImGui::InvisibleButton("scrollbar", ImVec2(trackWidth, thickness + 12 * s));
        bool hovered = ImGui::IsItemHovered(), active = ImGui::IsItemActive();
        if (ImGui::IsItemActivated()){
            float at = ImGui::GetMousePos().x;
            if (at < thumbX || at > thumbX + thumbWidth) tones.scroll = (at - left - thumbWidth / 2) / (trackWidth - thumbWidth) * maxScroll; // jump there
        }
        if (active) tones.scroll += ImGui::GetIO().MouseDelta.x * maxScroll / (trackWidth - thumbWidth);
        tones.scroll = std::clamp(tones.scroll, 0.0f, maxScroll);
        thumbX = left + (trackWidth - thumbWidth) * tones.scroll / maxScroll;
        draw->AddRectFilled(ImVec2(left, trackY), ImVec2(right, trackY + thickness), uiColor(UiColor::StaffLine, 0.8f), thickness / 2);
        draw->AddRectFilled(ImVec2(thumbX, trackY), ImVec2(thumbX + thumbWidth, trackY + thickness),
                            uiColor(active ? UiColor::Accent : UiColor::Dim, active || hovered ? 1.0f : 0.6f), thickness / 2);
    }

    // The move or removal clicked, now the cards are drawn
    std::vector<Effect>& effects = tones.editing.effects;
    if (action == Action::Left && actionAt > 0) std::swap(effects[actionAt], effects[actionAt - 1]);
    if (action == Action::Right && actionAt + 1 < (int)effects.size()) std::swap(effects[actionAt], effects[actionAt + 1]);
    if (action == Action::Remove) effects.erase(effects.begin() + actionAt);
    if (edited || action != Action::None) changed(settings);

    // Under the board: what the knob under the mouse does, else the card's effect
    if (tones.hoveredKnob){
        const char* effectName = tones.hovered >= 0 && tones.hovered < (int)tones.editing.effects.size()
                               ? effectInfo(tones.editing.effects[tones.hovered].type).name : "Out";
        float y = boardTop + cardHeight + 34 * s;
        const char* title = TextFormat("%s  ·  %s", effectName, tones.hoveredKnob->name);
        draw->AddText(fonts.bold, 16 * s, ImVec2(left, y), uiColor(UiColor::Ink), title);
        float titleWidth = fonts.bold ? fonts.bold->CalcTextSizeA(16 * s, FLT_MAX, 0.0f, title).x : 160 * s;
        draw->AddText(fonts.text, 16 * s, ImVec2(left + titleWidth + 12 * s, y), uiColor(UiColor::Dim),
                      tones.hoveredKnob->description, nullptr, right - left - titleWidth - 12 * s);
    } else if (tones.hovered >= 0 && tones.hovered < (int)tones.editing.effects.size()){
        const EffectInfo& info = effectInfo(tones.editing.effects[tones.hovered].type);
        draw->AddText(fonts.text, 16 * s, ImVec2(left, boardTop + cardHeight + 34 * s), uiColor(UiColor::Dim),
                      TextFormat("%s: %s", info.name, info.description));
    }

    // Saved a moment after the last change, once no knob is held
    if (tones.dirty && !ImGui::IsMouseDown(ImGuiMouseButton_Left) && GetTime() - tones.changedAt > SAVE_AFTER_S) save();
    menuScreenHint("Drag a knob up or down, Shift for fine    Double-click resets it    Drop a .tone file to add it, a .wav for a cabinet, a .nam capture    Esc  back", s);
    ImGui::End();
}
