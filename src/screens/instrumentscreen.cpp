#include "screens/instrumentscreen.h"

#include "screens/tonewizard.h"

#include "audio/audio.h"
#include "core/paths.h"
#include "core/chords.h"
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
#include "views/playnote.h"
#include "ui/ui.h"

#include <algorithm>
#include <cmath>
#include <ctime>
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
const double SAME_PLUCK_S = 0.06;  // notes this close in time were plucked together
const float TRAVEL_S = 0.16f;      // the light going from the note before to a new one
const float PATH_S = 1.2f;         // the dotted way between them, fading after
const float PATH_DOT_GAP = 14.0f;  // at a 720-pixel-tall window

// Play mode's notes (views/neckview), on this neck: a rounded card in its string's color with its fret on it and its
// name under, the newest lit white. `grow`: pixels added all round (a pop, a ring); `alpha` fades it.
static ImU32 imColor(Color color, float alpha = 1.0f){
    return IM_COL32(color.r, color.g, color.b, (int)(color.a * std::clamp(alpha, 0.0f, 1.0f)));
}
static Color blend(Color from, Color to, float t){
    return { (unsigned char)(from.r + (to.r - from.r) * t), (unsigned char)(from.g + (to.g - from.g) * t),
             (unsigned char)(from.b + (to.b - from.b) * t), 255 };
}
static void cardSize(const FretboardLayout& board, float& halfW, float& halfH){
    halfW = std::min(board.spacing * 0.27f * 1.5f, board.fretWidth * 0.38f);
    halfH = halfW / 1.5f;
}
static void cardOutline(ImDrawList* draw, ImVec2 at, float halfW, float halfH, float grow, ImU32 color, float width){
    const float w = halfW + grow, h = halfH + grow;
    draw->AddRect(ImVec2(at.x - w, at.y - h), ImVec2(at.x + w, at.y + h), color, std::max(0.0f, 0.4f * halfH + grow), 0, width);
}
static void drawNoteCard(ImDrawList* draw, const FretboardLayout& board, int string, int fret, int pitch, float grow, float alpha,
                         bool newest, float s){
    const ImVec2 at(board.fretX(fret), board.stringY(string));
    float halfW, halfH;
    cardSize(board, halfW, halfH);
    halfW += grow;
    halfH += grow;
    const bool night = currentTheme() == ThemeMode::Dark;
    const Color color = stringColor(string), card = themeColor(UiColor::Card), background = themeColor(UiColor::Background);
    const Color whiteHot = { 236, 246, 255, 255 };
    const float corner = 0.4f * halfH;
    // Its glow, fading out from its edge, then a dark rim that keeps notes apart
    for (int k = 1; k <= 3; k++) cardOutline(draw, at, halfW, halfH, 1.6f * k * s, imColor(newest ? whiteHot : color, alpha * (newest ? 0.4f : 0.22f) / k), 2.0f * s);
    draw->AddRectFilled(ImVec2(at.x - halfW - s, at.y - halfH - s), ImVec2(at.x + halfW + s, at.y + halfH + s), imColor(background, alpha), corner + s);
    const Color fill = newest ? (night ? whiteHot : color) : blend(card, color, night ? 0.16f : 0.14f);
    const Color ink = newest ? (night ? background : WHITE) : (night ? blend(color, WHITE, 0.35f) : blend(color, BLACK, 0.2f));
    draw->AddRectFilled(ImVec2(at.x - halfW, at.y - halfH), ImVec2(at.x + halfW, at.y + halfH), imColor(fill, alpha), corner);
    draw->AddRect(ImVec2(at.x - halfW + s, at.y - halfH + s), ImVec2(at.x + halfW - s, at.y + halfH - s), imColor(color, alpha), corner, 0, (newest ? 2.6f : 2.0f) * s);
    // The fret, and the name under it
    const UiFonts& fonts = uiFonts();
    const float size = halfH * 1.45f;
    const char* number = TextFormat("%d", fret);
    ImVec2 numberSize = fonts.bold->CalcTextSizeA(size * 0.8f, FLT_MAX, 0.0f, number);
    draw->AddText(fonts.bold, size * 0.8f, ImVec2(at.x - numberSize.x / 2, at.y - size * 0.2f - numberSize.y / 2), imColor(ink, alpha), number);
    const char* name = pitchClassName(pitch);
    ImVec2 nameSize = fonts.bold->CalcTextSizeA(size * 0.46f, FLT_MAX, 0.0f, name);
    draw->AddText(fonts.bold, size * 0.46f, ImVec2(at.x - nameSize.x / 2, at.y + size * 0.42f - nameSize.y / 2), imColor(ink, alpha * 0.85f), name);
}

// The hand's way from one note to the next: dots between their cards, from `a` to `b`. `light`: 0 to 1, a light going
// along it (negative: none); `alpha` fades the dots.
static void drawWay(ImDrawList* draw, const FretboardLayout& board, ImVec2 a, ImVec2 b, float light, float alpha, float s){
    float halfW, halfH;
    cardSize(board, halfW, halfH);
    const float dx = b.x - a.x, dy = b.y - a.y, length = std::sqrt(dx * dx + dy * dy);
    if (length < 1e-3f) return;
    const float ux = dx / length, uy = dy / length;
    // Where the way leaves a card: its edge in that direction
    const float edge = std::min(std::fabs(ux) > 1e-3f ? halfW / std::fabs(ux) : 1e9f, std::fabs(uy) > 1e-3f ? halfH / std::fabs(uy) : 1e9f) + 4 * s;
    float start = edge, end = length - edge;
    if (end <= start) return; // touching
    const ImU32 accent = uiColor(UiColor::Accent, 0.6f * alpha);
    const float gap = PATH_DOT_GAP * s;
    if (end - start < gap) start = end = (start + end) / 2;
    for (float d = start; d <= end + 0.01f; d += gap){
        if (light >= 0.0f && d > start + (end - start) * light) break; // laid down by the light as it passes
        draw->AddCircleFilled(ImVec2(a.x + ux * d, a.y + uy * d), 2.6f * s, accent);
    }
    if (light < 0.0f || light >= 1.0f) return;
    for (int k = 5; k >= 0; k--){
        const float along = start + (end - start) * std::max(0.0f, light - 0.05f * k), fade = 1.0f - k / 6.0f;
        draw->AddCircleFilled(ImVec2(a.x + ux * along, a.y + uy * along), (2.5f + 2.5f * fade) * s, uiColor(UiColor::Accent, 0.9f * fade));
    }
}

struct PlayedPlace {
    int pitch;
    StringFret place;
    double at; // when it was plucked (GetTime seconds)
};

// Its name is used by no other file: Visual Studio names an unnamed struct after its variable, and two files'
// structs named alike would share one constructor (the Settings screen's crashed on Windows when this was `screen`)
static struct {
    Settings settings;
    Settings* heard = nullptr; // the app's: the instrument chosen here is the one whose tone is heard
    Instrument instrument = Instrument::Guitar; // kept between visits
    bool listening = false;
    std::string error;
    std::string recordingSaved;      // where the last check was saved, to say so
    std::string chordName;           // the chord the latest strum made, heard from its whole sound (core/chords)...
    double chordAt = -100.0;         // ...and when it was struck
    // Guitar and bass
    std::deque<PlayedPlace> played; // the newest first
    StringFret hand{-1, -1};        // where the last note was played: the next is looked for near it
    float cents = 0.0f;
    // Piano
    double pressedAt[128] = {};
    double releasedAt[128] = {};
    bool wasDown[128] = {};
    std::string shownName, shownNotes; // the chord or notes held; kept after letting go, until the next press
} instrumentView;

static const std::vector<int>& tuning(){ return instrumentView.instrument == Instrument::Bass ? BASS_TUNING : GUITAR_TUNING; }
static int frets(){ return instrumentView.instrument == Instrument::Bass ? BASS_FRETS : GUITAR_FRETS; }

static void stopListening(){
    stopNoteInput();
    stopMidiInput();
    stopPianoKeys();
    instrumentView.listening = false;
}

static void startListening(){
    stopListening();
    instrumentView.error.clear();
    instrumentView.played.clear();
    instrumentView.hand = {-1, -1};
    instrumentView.shownName.clear();
    instrumentView.shownNotes.clear();
    std::fill(std::begin(instrumentView.wasDown), std::end(instrumentView.wasDown), false);
    std::fill(std::begin(instrumentView.releasedAt), std::end(instrumentView.releasedAt), -100.0);
    std::fill(std::begin(instrumentView.pressedAt), std::end(instrumentView.pressedAt), -100.0);
    if (instrumentView.instrument == Instrument::Piano){
        std::string midiError;
        startMidiInput(instrumentView.settings.midiDevice, midiError); // without a MIDI keyboard, the computer keys still play
        startPianoKeys(instrumentView.settings.pianoKeys, 48);
        instrumentView.listening = true;
        return;
    }
    InputRole role = instrumentView.instrument == Instrument::Bass ? InputRole::Bass : InputRole::Guitar;
    if (instrumentView.heard) hearInstrument(*instrumentView.heard, role); // its own tone
    float lowest = midiToFrequency((float)tuning().front()) * 0.9f;
    instrumentView.listening = startNoteInput(instrumentView.settings.inputDevice, lowest, instrumentView.error, channelFor(instrumentView.settings, role));
}

void openInstrumentScreen(Settings& settings){
    instrumentView.settings = settings;
    instrumentView.heard = &settings;
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
    Instrument chosen = instrumentView.instrument;
    for (int i = 0; i < 3; i++){
        const char* name = INSTRUMENT_NAMES[i];
        bool on = (int)instrumentView.instrument == i;
        ImVec2 size = fonts.bold ? fonts.bold->CalcTextSizeA(20 * s, FLT_MAX, 0.0f, name) : ImVec2(60 * s, 20 * s);
        draw->AddText(fonts.bold, 20 * s, ImVec2(x, y), uiColor(on ? UiColor::Ink : UiColor::Dim), name);
        if (on) draw->AddRectFilled(ImVec2(x, y + size.y + 3 * s), ImVec2(x + size.x, y + size.y + 5 * s), uiColor(UiColor::Accent));
        ImVec2 mouse = ImGui::GetMousePos();
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && mouse.x >= x && mouse.x <= x + size.x && mouse.y >= y && mouse.y <= y + size.y){
            chosen = (Instrument)i;
        }
        x += size.x + 22 * s;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Tab)) chosen = (Instrument)(((int)instrumentView.instrument + 1) % 3);
    if (chosen != instrumentView.instrument){
        instrumentView.instrument = chosen;
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
    if (!instrumentView.error.empty()){
        std::string hint = "Choose the input in Settings, Instruments";
        draw->AddText(fonts.text, 16 * s, ImVec2(right - widthOf(fonts.text, 16 * s, instrumentView.error), y - 24 * s), uiColor(UiColor::Bad), instrumentView.error.c_str());
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
    const double now = GetTime();
    std::deque<PlayedPlace>& played = instrumentView.played;
    for (const PlayedNote& note : updateNoteInput()){
        StringFret place = likeliestPosition(positionsOf(note.pitch, tuning(), frets()), instrumentView.hand);
        if (place.string < 0) continue; // off this neck: lower than its lowest string, or past its last fret
        instrumentView.hand = place;
        instrumentView.cents = note.cents;
        played.push_front({note.pitch, place, now - note.age});
    }
    // A pluck of several notes: together they take the place of the one note heard for it (one of them, or a muddle)
    for (const PlayedChord& chord : noteInputChords()){
        const double at = now - chord.age;
        instrumentView.chordName = chord.name;
        instrumentView.chordAt = at;
        played.erase(std::remove_if(played.begin(), played.end(), [&](const PlayedPlace& note){ return std::fabs(note.at - at) < SAME_PLUCK_S; }), played.end());
        StringFret before = played.empty() ? StringFret{-1, -1} : played.front().place;
        std::vector<StringFret> places = chordPositions(chord.pitches, tuning(), frets(), before);
        for (size_t i = 0; i < places.size(); i++){
            if (places[i].string < 0) continue;
            played.push_front({chord.pitches[i], places[i], at});
            instrumentView.hand = places[i];
        }
    }
    // The newest pluck's notes are at the front (one, or the several of a chord); the trail is what came before
    int together = 0;
    while (together < (int)played.size() && std::fabs(played[together].at - played.front().at) < SAME_PLUCK_S) together++;
    while ((int)played.size() > TRAIL + std::max(1, together)) played.pop_back();

    ImDrawList* draw = ImGui::GetWindowDrawList();
    const float left = width * 0.1f;
    // The chord the newest strum made, heard from its whole sound: named even when only some of its notes were placed
    const bool chordHeard = !played.empty() && !instrumentView.chordName.empty() && std::fabs(instrumentView.chordAt - played.front().at) < SAME_PLUCK_S;
    if (together == 1 && chordHeard){
        drawHeading(ImVec2(left, height * 0.2f), s, instrumentView.chordName, true, noteName(played.front().pitch) + " and more", "CHORD", UiColor::Good);
    } else if (together == 1){
        const PlayedPlace& newest = played.front();
        std::string where = stringName(newest.place.string) + " string, " + (newest.place.fret == 0 ? std::string("open") : "fret " + std::to_string(newest.place.fret));
        int cents = (int)std::lround(instrumentView.cents);
        bool inTune = std::abs(cents) <= 5;
        std::string tune = inTune ? "IN TUNE" : TextFormat("%d CENTS %s", std::abs(cents), cents > 0 ? "SHARP" : "FLAT");
        drawHeading(ImVec2(left, height * 0.2f), s, noteName(newest.pitch), true, where, tune, inTune ? UiColor::Good : UiColor::Dim);
    } else if (together > 1){
        // Lowest first: "E2 + B2", each on its string, and the chord they make if they make one
        std::vector<PlayedPlace> chord(played.begin(), played.begin() + together);
        std::sort(chord.begin(), chord.end(), [](const PlayedPlace& a, const PlayedPlace& b){ return a.pitch < b.pitch; });
        std::string names, where;
        std::vector<int> pitches;
        for (const PlayedPlace& note : chord){
            names += (names.empty() ? "" : " + ") + noteName(note.pitch);
            where += (where.empty() ? "" : ", ") + stringName(note.place.string) + (note.place.fret == 0 ? std::string(" open") : " fret " + std::to_string(note.place.fret));
            pitches.push_back(note.pitch);
        }
        std::string made = chordHeard ? instrumentView.chordName : nameChord(pitches);
        drawHeading(ImVec2(left, height * 0.2f), s, names, true, where, made.empty() ? TextFormat("%d NOTES TOGETHER", together) : made, UiColor::Good);
    } else {
        drawHeading(ImVec2(left, height * 0.2f), s, "Play a note", false, "", "", UiColor::Dim);
    }

    // Strings far enough apart for play mode's note cards: as far as the room under the heading allows, up to 52
    const float spacing = std::min(52.0f, height * 0.4f / s / (float)tuning().size());
    FretboardLayout board = fretboardLayout(left, height * 0.34f, width * 0.82f, s, (int)tuning().size(), 0, frets(), spacing);
    drawFretboard(board, tuning());
    // The way the hand went: between each pluck and the next, faint dots fading with age; to the newest, a light that
    // travels there as it's played
    auto placeAt = [&](const PlayedPlace& note){ return ImVec2(board.fretX(note.place.fret), board.stringY(note.place.string)); };
    for (int i = (int)played.size() - 1; i > 0; i--){
        const PlayedPlace& from = played[i];
        int next = i - 1;
        while (next > 0 && std::fabs(played[next].at - from.at) < SAME_PLUCK_S) next--; // a chord's notes: one way, from its last
        if (std::fabs(played[next].at - from.at) < SAME_PLUCK_S) continue;
        const float since = (float)(now - played[next].at);
        if (played[next].at - from.at > 2.0) continue; // a pause: no way drawn across it
        const float light = since < TRAVEL_S ? since / TRAVEL_S : -1.0f;
        const float alpha = next < together ? std::max(0.0f, 1.0f - std::max(0.0f, since - TRAVEL_S) / PATH_S) * 0.9f + 0.1f
                                            : 0.25f * (1.0f - (float)i / (TRAIL + 2));
        drawWay(draw, board, placeAt(from), placeAt(played[next]), light, alpha, s);
    }
    // The notes before, oldest first so newer ones sit on top: a scale shows its shape
    for (int i = (int)played.size() - 1; i >= together; i--){
        const PlayedPlace& note = played[i];
        float fade = 1.0f - (float)(i - together + 1) / (TRAIL + 1);
        drawNoteCard(draw, board, note.place.string, note.place.fret, note.pitch, 0.0f, 0.2f + 0.55f * fade, false, s);
    }
    for (int i = together - 1; i >= 0; i--){
        const PlayedPlace& newest = played[i];
        float t = (float)(now - newest.at);
        const StringFret place = newest.place;
        // A note alone: its other places as outlines, since it may have been played there instead
        float halfW, halfH;
        cardSize(board, halfW, halfH);
        if (together == 1){
            for (StringFret other : positionsOf(newest.pitch, tuning(), frets())){
                if (other == place) continue;
                cardOutline(draw, ImVec2(board.fretX(other.fret), board.stringY(other.string)), halfW, halfH, 0.0f, uiColor(UiColor::Accent, 0.45f), 1.5f * s);
            }
        }
        // The string shakes from the fret to the bridge (past the board's right), settling
        if (t < SHAKE_S){
            float x0 = place.fret == 0 ? board.boardLeft : board.fretX(place.fret) + board.fretWidth / 2;
            float x1 = board.left + board.width - 6 * s, y = board.stringY(place.string);
            float amplitude = 5.0f * s * std::exp(-t * 2.5f), alpha = 1.0f - t / SHAKE_S;
            ImVec2 points[64];
            for (int point = 0; point < 64; point++){
                float u = point / 63.0f;
                float dy = amplitude * (std::sin(PI * u) * std::cos(2 * PI * 7.0f * t) + 0.3f * std::sin(2 * PI * u) * std::cos(2 * PI * 11.0f * t));
                points[point] = ImVec2(x0 + (x1 - x0) * u, y + dy);
            }
            draw->AddPolyline(points, 64, uiColor(UiColor::Accent, alpha), 0, 2.0f * s);
        }
        // The note: it pops in, and a ring of its shape goes out from it
        if (t < RING_S){
            float u = t / RING_S, eased = 1.0f - (1.0f - u) * (1.0f - u);
            cardOutline(draw, ImVec2(board.fretX(place.fret), board.stringY(place.string)), halfW, halfH, 4 * s + 22 * s * eased,
                        uiColor(UiColor::Accent, 0.6f * (1.0f - u)), 2.0f * s);
        }
        drawNoteCard(draw, board, place.string, place.fret, newest.pitch, 4.0f * s * std::exp(-t * 12.0f), 1.0f, true, s);
    }
    const UiFonts& fonts = uiFonts();
    const char* explain = "The bright one: where it was most likely played, near your last note. Outlines: the same note elsewhere. Strings plucked together show together, with their chord.";
    draw->AddText(fonts.text, 15 * s, ImVec2(left, board.top + board.height + 34 * s), uiColor(UiColor::Dim), explain);

    InputRole role = instrumentView.instrument == Instrument::Bass ? InputRole::Bass : InputRole::Guitar;
    int channel = channelFor(instrumentView.settings, role);
    std::string device = instrumentView.settings.inputDevice.empty() ? "default input" : instrumentView.settings.inputDevice;
    drawListening(width * 0.93f, height - 40 * s, s, device + (channel < 0 ? "  ·  all inputs" : "  ·  input " + std::to_string(channel + 1)), instrumentView.listening);

    // A check: what's played recorded, with what was heard in it, for finding out why a note is misheard
    if (instrumentView.listening){
        const bool recording = inputRecording();
        const double seconds = inputRecordingSeconds();
        const std::string text = recording ? TextFormat("Recording %d:%02d  ·  stop and save", (int)seconds / 60, (int)seconds % 60) : "Record a check";
        if (menuPill(text.c_str(), "R", ImVec2(left, height - 88 * s), false, 0, s) || ImGui::IsKeyPressed(ImGuiKey_R, false)){
            if (!recording){
                startInputRecording();
                instrumentView.recordingSaved.clear();
            } else {
                char stamp[32];
                std::time_t clock = std::time(nullptr);
                std::strftime(stamp, sizeof stamp, "%Y-%m-%d-%H%M%S", std::localtime(&clock));
                const std::string base = userDataDir() + "/check-" + (instrumentView.instrument == Instrument::Bass ? "bass-" : "guitar-") + stamp;
                std::string error;
                instrumentView.recordingSaved = saveInputRecording(base, error) ? "Saved: " + base + ".wav and .txt" : "Not saved: " + error;
            }
        }
        if (recording) draw->AddCircleFilled(ImVec2(left - 14 * s, height - 73 * s), 5 * s, uiColor(UiColor::Bad, 0.6f + 0.4f * (float)std::sin(now * 6.0)));
        // Where it went, and the way there: on a Mac the data folder is in the Library, which Finder hides
        if (!instrumentView.recordingSaved.empty()){
            draw->AddText(fonts.text, 14 * s, ImVec2(left, height - 112 * s), uiColor(UiColor::Dim), instrumentView.recordingSaved.c_str());
            if (instrumentView.recordingSaved.rfind("Saved", 0) == 0 && menuPill("Open the folder", "O", ImVec2(left + 260 * s, height - 88 * s), false, 0, s)) openFolder(userDataDir());
            if (instrumentView.recordingSaved.rfind("Saved", 0) == 0 && ImGui::IsKeyPressed(ImGuiKey_O, false)) openFolder(userDataDir());
        }
    }
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
            instrumentView.pressedAt[note.pitch] = now;
        }
    };
    if (midiInputActive()) pressed(updateMidiInput());
    if (pianoKeysActive()) pressed(updatePianoKeys());
    const bool* midi = midiInputActive() ? midiKeysDown() : nullptr;
    const bool* keys = pianoKeysActive() ? pianoKeysDown() : nullptr;
    bool down[128];
    bool anyPressed = false;
    std::vector<int> held;
    for (int pitch = 0; pitch < 128; pitch++){
        down[pitch] = (midi && midi[pitch]) || (keys && keys[pitch]);
        if (instrumentView.wasDown[pitch] && !down[pitch]) instrumentView.releasedAt[pitch] = now;
        if (down[pitch] && !instrumentView.wasDown[pitch]) anyPressed = true;
        instrumentView.wasDown[pitch] = down[pitch];
        if (down[pitch]) held.push_back(pitch);
    }
    // Named when a key goes down, not when one comes up: a chord let go of a note at a time stays named
    if (anyPressed){
        std::string notes;
        for (int pitch : held) notes += (notes.empty() ? "" : " ") + noteName(pitch);
        std::string chord = nameChord(held);
        instrumentView.shownName = chord.empty() ? notes : chord;
        instrumentView.shownNotes = chord.empty() ? "" : notes;
    }

    const float left = width * 0.1f;
    if (instrumentView.shownName.empty()) drawHeading(ImVec2(left, height * 0.2f), s, "Play a note", false, "", "", UiColor::Dim);
    else drawHeading(ImVec2(left, height * 0.2f), s, instrumentView.shownName, !held.empty(), instrumentView.shownNotes, "", UiColor::Dim);

    // The keyboard, centered
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const UiFonts& fonts = uiFonts();
    const int whites = pianoWhiteKeys(PIANO_KEYS);
    const float whiteWidth = std::floor(std::min(40 * s, width * 0.84f / whites)), keyHeight = 150 * s; // whole pixels: even gaps
    const ImVec2 origin(std::round((width - whiteWidth * whites) / 2), std::round(height * 0.45f));
    // A key just pressed sends a glow up, fading as it rises
    for (int key = 0; key < PIANO_KEYS; key++){
        float t = (float)(now - instrumentView.pressedAt[PIANO_LOW + key]);
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
        float light = down[pitch] ? 1.0f : std::max(0.0f, 1.0f - (float)(now - instrumentView.releasedAt[pitch]) / KEY_FADE_S);
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
    if (instrumentView.instrument == Instrument::Piano){
        pianoScreen(width, height, s);
        menuScreenHint("Tab  instrument    Up Down  octave of the computer keys    Esc  back", s);
    } else {
        frettedScreen(width, height, s);
        menuScreenHint("Tab  instrument    R  record a check    Esc  back", s);
    }
    ImGui::End();
}
