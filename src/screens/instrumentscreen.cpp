#include "screens/instrumentscreen.h"

#include "audio/audio.h"
#include "core/music.h"
#include "core/positions.h"
#include "imgui.h"
#include "input/midi.h"
#include "input/noteinput.h"
#include "input/pianokeys.h"
#include "raylib.h"
#include "ui/fretboardview.h"
#include "ui/menulist.h"
#include "ui/pianoview.h"
#include "ui/theme.h"
#include "ui/ui.h"

#include <algorithm>
#include <cmath>
#include <deque>

enum class Instrument { Guitar, Bass, Piano };
const char* const INSTRUMENT_NAMES[] = { "GUITAR", "BASS", "PIANO" };
const std::vector<int> GUITAR_TUNING = { 40, 45, 50, 55, 59, 64 }; // standard: E A D G B E
const std::vector<int> BASS_TUNING = { 28, 33, 38, 43 };           // E A D G
const int GUITAR_FRETS = 22, BASS_FRETS = 20;
const int TRAIL = 6;               // the notes before the newest, fading out behind it
const float RING_S = 0.5f;         // the ring that goes out from a new note
const float SHAKE_S = 1.6f;        // how long a plucked string is seen shaking
const int PIANO_LOW = 36, PIANO_KEYS = 61; // C2 to C7, a 61-key keyboard
const float KEY_FADE_S = 0.35f;    // a released key's light going out
const float GLOW_S = 0.45f;        // the glow rising from a key just pressed

struct PlayedPlace {
    int pitch;
    StringFret place;
    double at; // GetTime seconds
};

static struct {
    Settings settings;
    Instrument instrument = Instrument::Guitar; // kept between visits
    bool listening = false;
    std::string error;
    // Guitar and bass
    std::deque<PlayedPlace> played; // the newest first
    StringFret hand{-1, -1};        // where the last note was played: the next is looked for near it
    float cents = 0.0f;
    // Piano
    double pressedAt[128] = {};
    double releasedAt[128] = {};
    bool wasDown[128] = {};
    int lastPitch = -1;
} screen;

static const std::vector<int>& tuning(){ return screen.instrument == Instrument::Bass ? BASS_TUNING : GUITAR_TUNING; }
static int frets(){ return screen.instrument == Instrument::Bass ? BASS_FRETS : GUITAR_FRETS; }

static void stopListening(){
    stopNoteInput();
    stopMidiInput();
    stopPianoKeys();
    screen.listening = false;
}

static void startListening(){
    stopListening();
    screen.error.clear();
    screen.played.clear();
    screen.hand = {-1, -1};
    screen.lastPitch = -1;
    std::fill(std::begin(screen.wasDown), std::end(screen.wasDown), false);
    std::fill(std::begin(screen.releasedAt), std::end(screen.releasedAt), -100.0);
    std::fill(std::begin(screen.pressedAt), std::end(screen.pressedAt), -100.0);
    if (screen.instrument == Instrument::Piano){
        std::string midiError;
        startMidiInput(screen.settings.midiDevice, midiError); // without a MIDI keyboard, the computer keys still play
        startPianoKeys(screen.settings.pianoKeys, 48);
        screen.listening = true;
        return;
    }
    InputRole role = screen.instrument == Instrument::Bass ? InputRole::Bass : InputRole::Guitar;
    float lowest = midiToFrequency((float)tuning().front()) * 0.9f;
    screen.listening = startNoteInput(screen.settings.inputDevice, lowest, screen.error, channelFor(screen.settings, role));
}

void openInstrumentScreen(const Settings& settings){
    screen.settings = settings;
    startListening();
}

void closeInstrumentScreen(){
    stopListening();
}

// "G", or "low E" and "high E" where two strings share a name
static std::string stringName(int string){
    const std::vector<int>& strings = tuning();
    std::string name = pitchClassName(strings[string]);
    int same = 0;
    for (int other : strings) if (other % 12 == strings[string] % 12) same++;
    if (same < 2) return name;
    bool lowest = std::none_of(strings.begin(), strings.begin() + string, [&](int other){ return other % 12 == strings[string] % 12; });
    return (lowest ? "low " : "high ") + name;
}

static std::string noteName(int pitch){ return std::string(pitchClassName(pitch)) + std::to_string(pitchOctave(pitch)); }

// The instruments to choose from, beside the title: Tab or a click
static void drawInstrumentSwitch(float s){
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const UiFonts& fonts = uiFonts();
    float x = ImGui::GetWindowWidth() * 0.55f, y = ImGui::GetWindowHeight() * 0.09f + 14 * s;
    Instrument chosen = screen.instrument;
    for (int i = 0; i < 3; i++){
        const char* name = INSTRUMENT_NAMES[i];
        bool on = (int)screen.instrument == i;
        ImVec2 size = fonts.bold ? fonts.bold->CalcTextSizeA(20 * s, FLT_MAX, 0.0f, name) : ImVec2(60 * s, 20 * s);
        draw->AddText(fonts.bold, 20 * s, ImVec2(x, y), uiColor(on ? UiColor::Ink : UiColor::Dim), name);
        if (on) draw->AddRectFilled(ImVec2(x, y + size.y + 3 * s), ImVec2(x + size.x, y + size.y + 5 * s), uiColor(UiColor::Accent));
        ImVec2 mouse = ImGui::GetMousePos();
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && mouse.x >= x && mouse.x <= x + size.x && mouse.y >= y && mouse.y <= y + size.y){
            chosen = (Instrument)i;
        }
        x += size.x + 22 * s;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Tab)) chosen = (Instrument)(((int)screen.instrument + 1) % 3);
    if (chosen != screen.instrument){
        screen.instrument = chosen;
        startListening();
    }
}

// The note's name, big, and what goes with it
static void drawHeading(ImVec2 at, float s, const std::string& name, bool strong, const std::string& detail, const std::string& small, UiColor smallColor){
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const UiFonts& fonts = uiFonts();
    const float size = 84 * s;
    draw->AddText(fonts.heavy, size, at, uiColor(strong ? UiColor::Ink : UiColor::Dim), name.c_str());
    float width = fonts.heavy ? fonts.heavy->CalcTextSizeA(size, FLT_MAX, 0.0f, name.c_str()).x : size;
    float x = at.x + width + 24 * s;
    draw->AddText(fonts.bold, 24 * s, ImVec2(x, at.y + 22 * s), uiColor(UiColor::Dim), detail.c_str());
    draw->AddText(fonts.mono, 14 * s, ImVec2(x, at.y + 56 * s), uiColor(smallColor), small.c_str());
}

// What the screen listens to, at the bottom right (the keys to press are at the left)
static void drawListening(float right, float y, float s, const std::string& what, bool meter){
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const UiFonts& fonts = uiFonts();
    auto widthOf = [&](ImFont* font, float size, const std::string& text){
        return font ? font->CalcTextSizeA(size, FLT_MAX, 0.0f, text.c_str()).x : text.size() * size * 0.6f;
    };
    if (!screen.error.empty()){
        std::string hint = "Choose the input in Settings, Instruments";
        draw->AddText(fonts.text, 16 * s, ImVec2(right - widthOf(fonts.text, 16 * s, screen.error), y - 24 * s), uiColor(UiColor::Bad), screen.error.c_str());
        draw->AddText(fonts.mono, 14 * s, ImVec2(right - widthOf(fonts.mono, 14 * s, hint), y), uiColor(UiColor::Dim), hint.c_str());
        return;
    }
    // How loud the input is: seeing it move tells the player the right input is listening
    const float barWidth = meter ? 90 * s : 0.0f;
    if (meter){
        float level = std::clamp((noteInputLevelDb() + 60.0f) / 60.0f, 0.0f, 1.0f);
        draw->AddRectFilled(ImVec2(right - barWidth, y + 5 * s), ImVec2(right, y + 9 * s), uiColor(UiColor::StaffLine), 2 * s);
        draw->AddRectFilled(ImVec2(right - barWidth, y + 5 * s), ImVec2(right - barWidth * (1.0f - level), y + 9 * s), uiColor(UiColor::Accent), 2 * s);
    }
    std::string text = "IN  ·  " + what;
    float x = right - barWidth - (meter ? 16 * s : 0.0f) - widthOf(fonts.mono, 14 * s, text);
    draw->AddText(fonts.mono, 14 * s, ImVec2(x, y), uiColor(UiColor::Dim), text.c_str());
}

static void frettedScreen(float width, float height, float s){
    for (const PlayedNote& note : updateNoteInput()){
        StringFret place = likeliestPosition(positionsOf(note.pitch, tuning(), frets()), screen.hand);
        if (place.string < 0) continue; // off this neck: lower than its lowest string, or past its last fret
        screen.hand = place;
        screen.cents = note.cents;
        screen.played.push_front({note.pitch, place, GetTime()});
        if ((int)screen.played.size() > TRAIL + 1) screen.played.pop_back();
    }
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const double now = GetTime();
    const float left = width * 0.1f;
    const PlayedPlace* newest = screen.played.empty() ? nullptr : &screen.played.front();

    if (newest){
        std::string where = stringName(newest->place.string) + " string, " + (newest->place.fret == 0 ? std::string("open") : "fret " + std::to_string(newest->place.fret));
        int cents = (int)std::lround(screen.cents);
        bool inTune = std::abs(cents) <= 5;
        std::string tune = inTune ? "IN TUNE" : TextFormat("%d CENTS %s", std::abs(cents), cents > 0 ? "SHARP" : "FLAT");
        drawHeading(ImVec2(left, height * 0.2f), s, noteName(newest->pitch), true, where, tune, inTune ? UiColor::Good : UiColor::Dim);
    } else {
        drawHeading(ImVec2(left, height * 0.2f), s, "Play a note", false, "", "", UiColor::Dim);
    }

    FretboardLayout board = fretboardLayout(left, height * 0.43f, width * 0.82f, s, (int)tuning().size(), 0, frets());
    drawFretboard(board, tuning());
    // The notes before, oldest first so newer ones sit on top: a scale shows its shape
    for (int i = (int)screen.played.size() - 1; i >= 1; i--){
        const PlayedPlace& note = screen.played[i];
        float fade = 1.0f - (float)i / (TRAIL + 1);
        drawFretDot(board, note.place.string, note.place.fret, 9 * s, uiColor(UiColor::Accent, 0.12f + 0.4f * fade), 0, nullptr);
    }
    if (newest){
        float t = (float)(now - newest->at);
        const StringFret place = newest->place;
        // The same note's other places: rings, since it may have been played there instead
        for (StringFret other : positionsOf(newest->pitch, tuning(), frets())){
            if (other == place) continue;
            draw->AddCircle(ImVec2(board.fretX(other.fret), board.stringY(other.string)), 11 * s, uiColor(UiColor::Accent, 0.6f), 0, 1.5f * s);
        }
        // The string shakes from the fret to the bridge (past the board's right), settling
        if (t < SHAKE_S){
            float x0 = place.fret == 0 ? board.boardLeft : board.fretX(place.fret) + board.fretWidth / 2;
            float x1 = board.left + board.width - 6 * s, y = board.stringY(place.string);
            float amplitude = 5.0f * s * std::exp(-t * 2.5f), alpha = 1.0f - t / SHAKE_S;
            ImVec2 points[64];
            for (int i = 0; i < 64; i++){
                float u = i / 63.0f;
                float dy = amplitude * (std::sin(PI * u) * std::cos(2 * PI * 7.0f * t) + 0.3f * std::sin(2 * PI * u) * std::cos(2 * PI * 11.0f * t));
                points[i] = ImVec2(x0 + (x1 - x0) * u, y + dy);
            }
            draw->AddPolyline(points, 64, uiColor(UiColor::Accent, alpha), 0, 2.0f * s);
        }
        // The note: it pops in, and a ring goes out from it
        float radius = 13 * s * (1.0f + 0.3f * std::exp(-t * 12.0f));
        if (t < RING_S){
            float u = t / RING_S, eased = 1.0f - (1.0f - u) * (1.0f - u);
            draw->AddCircle(ImVec2(board.fretX(place.fret), board.stringY(place.string)), 13 * s + 26 * s * eased,
                            uiColor(UiColor::Accent, 0.6f * (1.0f - u)), 0, 2.0f * s);
        }
        drawFretDot(board, place.string, place.fret, radius, uiColor(UiColor::Accent), uiColor(UiColor::Card), pitchClassName(newest->pitch));
    }
    const UiFonts& fonts = uiFonts();
    const char* explain = "The bright one: where it was most likely played, near your last note. Rings: the same note elsewhere.";
    draw->AddText(fonts.text, 15 * s, ImVec2(left, board.top + board.height + 34 * s), uiColor(UiColor::Dim), explain);

    InputRole role = screen.instrument == Instrument::Bass ? InputRole::Bass : InputRole::Guitar;
    int channel = channelFor(screen.settings, role);
    std::string device = screen.settings.inputDevice.empty() ? "default input" : screen.settings.inputDevice;
    drawListening(width * 0.93f, height - 40 * s, s, device + (channel < 0 ? "  ·  all inputs" : "  ·  input " + std::to_string(channel + 1)), screen.listening);
}

static ImU32 mix(ImU32 from, ImU32 to, float t){
    ImVec4 a = ImGui::ColorConvertU32ToFloat4(from), b = ImGui::ColorConvertU32ToFloat4(to);
    return ImGui::ColorConvertFloat4ToU32(ImVec4(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, 1.0f));
}

static void pianoScreen(float width, float height, float s){
    const double now = GetTime();
    auto pressed = [&](const std::vector<PlayedNote>& notes){
        for (const PlayedNote& note : notes){
            playKeysNote(midiToFrequency((float)note.pitch));
            screen.pressedAt[note.pitch] = now;
            screen.lastPitch = note.pitch;
        }
    };
    if (midiInputActive()) pressed(updateMidiInput());
    if (pianoKeysActive()) pressed(updatePianoKeys());
    const bool* midi = midiInputActive() ? midiKeysDown() : nullptr;
    const bool* keys = pianoKeysActive() ? pianoKeysDown() : nullptr;
    bool down[128];
    std::string held;
    for (int pitch = 0; pitch < 128; pitch++){
        down[pitch] = (midi && midi[pitch]) || (keys && keys[pitch]);
        if (screen.wasDown[pitch] && !down[pitch]) screen.releasedAt[pitch] = now;
        screen.wasDown[pitch] = down[pitch];
        if (down[pitch]) held += (held.empty() ? "" : " ") + noteName(pitch);
    }

    const float left = width * 0.1f;
    if (!held.empty()) drawHeading(ImVec2(left, height * 0.2f), s, held, true, "", "", UiColor::Dim);
    else if (screen.lastPitch >= 0) drawHeading(ImVec2(left, height * 0.2f), s, noteName(screen.lastPitch), false, "", "", UiColor::Dim);
    else drawHeading(ImVec2(left, height * 0.2f), s, "Play a note", false, "", "", UiColor::Dim);

    // The keyboard, centered
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const UiFonts& fonts = uiFonts();
    const int whites = pianoWhiteKeys(PIANO_KEYS);
    const float whiteWidth = std::floor(std::min(40 * s, width * 0.84f / whites)), keyHeight = 150 * s; // whole pixels: even gaps
    const ImVec2 origin(std::round((width - whiteWidth * whites) / 2), std::round(height * 0.45f));
    // A key just pressed sends a glow up, fading as it rises
    for (int key = 0; key < PIANO_KEYS; key++){
        float t = (float)(now - screen.pressedAt[PIANO_LOW + key]);
        if (t >= GLOW_S) continue;
        float u = t / GLOW_S;
        ImVec4 r = pianoKeyRect(origin, whiteWidth, keyHeight, key);
        float rise = 70 * s * (0.6f + 0.4f * u);
        ImU32 clear = uiColor(UiColor::Accent, 0.0f), glow = uiColor(UiColor::Accent, 0.5f * (1.0f - u));
        draw->AddRectFilledMultiColor(ImVec2(r.x, r.y - rise), ImVec2(r.x + r.z, r.y), clear, clear, glow, glow);
    }
    drawPianoKeys(origin, whiteWidth, keyHeight, PIANO_KEYS, [&](int key, bool){
        int pitch = PIANO_LOW + key;
        PianoKeyStyle look;
        float light = down[pitch] ? 1.0f : std::max(0.0f, 1.0f - (float)(now - screen.releasedAt[pitch]) / KEY_FADE_S);
        if (light > 0.0f) look.fill = mix(pianoKeyColor(key), uiColor(UiColor::Accent), light);
        if (keys) look.label = pianoKeyFor(pitch);
        if (!pianoKeyIsBlack(key) && light < 0.5f) look.ink = uiColor(UiColor::Ink); // what reads on white
        return look;
    });
    // The Cs named below, to find your way
    for (int key = 0; key < PIANO_KEYS; key += 12){
        ImVec4 r = pianoKeyRect(origin, whiteWidth, keyHeight, key);
        std::string name = noteName(PIANO_LOW + key);
        float nameWidth = fonts.mono ? fonts.mono->CalcTextSizeA(13 * s, FLT_MAX, 0.0f, name.c_str()).x : 0.0f;
        draw->AddText(fonts.mono, 13 * s, ImVec2(r.x + (r.z - nameWidth) / 2, r.y + r.w + 8 * s), uiColor(UiColor::Dim), name.c_str());
    }

    std::string what = midiInputActive() ? std::string(midiDeviceName()) + "  ·  and the computer keys" : "the computer keys  ·  no MIDI keyboard found";
    drawListening(width * 0.93f, height - 40 * s, s, what, false);
}

void instrumentScreen(){
    beginMenu("Instrument");
    const float s = menuScale(), width = ImGui::GetWindowWidth(), height = ImGui::GetWindowHeight();
    menuScreenTitle("Instrument", s);
    drawInstrumentSwitch(s);
    if (screen.instrument == Instrument::Piano){
        pianoScreen(width, height, s);
        menuScreenHint("Tab  instrument    Up Down  octave of the computer keys    Esc  back", s);
    } else {
        frettedScreen(width, height, s);
        menuScreenHint("Tab  instrument    Esc  back", s);
    }
    ImGui::End();
}
