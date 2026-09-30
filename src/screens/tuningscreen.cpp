#include "screens/tuningscreen.h"

#include "audio/audio.h"
#include "core/music.h"
#include "core/pitch.h"
#include "core/tuningcheck.h"
#include "imgui.h"
#include "raylib.h"
#include "ui/menulist.h"
#include "ui/theme.h"
#include "ui/ui.h"

#include <algorithm>
#include <cmath>
#include <cstring>

const float SMOOTHING = 0.3f;             // of the gap to a new reading the needle closes each frame
const float NOTE_CHANGE_SEMITONES = 0.5f; // a bigger jump is another string: snap instead of gliding
const double HEARD_HOLD_S = 0.5;          // a string stays lit this long after it's last heard
const double DONE_PAUSE_S = 1.0;          // all in tune: the song starts after this long, the moment enjoyed
const float SHOWN_CENTS = 50.0f;          // the meters' reach, either way

static struct {
    TuningCheck check;
    InputRole instrument = InputRole::Bass;
    int channel = -1;
    std::string reason;
    PitchDetector detector;
    std::vector<float> window;   // the latest samples, oldest first
    std::vector<float> incoming;
    NoiseFloor floor;
    float levelDb = -100.0f;
    float midi = 0.0f;           // smoothed, as heard
    std::vector<float> needle;   // each string's needle, gliding to its cents
    double heardAt = -100.0;     // GetTime of the last pitch heard
    double doneAt = -1.0;        // when every string was in tune
    std::vector<double> tunedAt; // when each string got there, for its flash
    bool active = false;
} check;

bool openTuningScreen(const std::vector<int>& tuning, InputRole instrument, const std::string& inputDevice, int channel,
                      const std::string& reason, std::string& error){
    closeTuningScreen();
    if (tuning.empty()){
        error = "the part has no strings to tune";
        return false;
    }
    if (!startCapture(inputDevice, error)) return false;
    // From under the lowest string to past the highest's octave, where a string's harmonics can be taken for it
    int lowest = *std::min_element(tuning.begin(), tuning.end()), highest = *std::max_element(tuning.begin(), tuning.end());
    initPitchDetector(check.detector, captureSampleRate(), midiToFrequency((float)lowest) * 0.8f, midiToFrequency((float)highest + 14.0f));
    check.window.assign(pitchWindowSize(check.detector), 0.0f);
    check.incoming.assign(check.window.size(), 0.0f);
    check.check = startTuningCheck(tuning);
    check.instrument = instrument;
    check.channel = channel;
    check.reason = reason;
    check.floor = {};
    check.levelDb = -100.0f;
    check.midi = 0.0f;
    check.needle.assign(tuning.size(), 0.0f);
    check.tunedAt.assign(tuning.size(), -100.0);
    check.heardAt = -100.0;
    check.doneAt = -1.0;
    check.active = true;
    return true;
}

void closeTuningScreen(){
    if (!check.active) return;
    stopCapture();
    check.active = false;
}

// A pitch heard (a fractional MIDI note) for `seconds`: the check hears it, and a string reaching its note chimes
static void hear(float midi, float seconds){
    bool gliding = GetTime() - check.heardAt < HEARD_HOLD_S && std::fabs(midi - check.midi) <= NOTE_CHANGE_SEMITONES;
    check.midi = gliding ? check.midi + (midi - check.midi) * SMOOTHING : midi;
    check.heardAt = GetTime();
    std::vector<bool> before;
    for (const StringCheck& string : check.check.strings) before.push_back(string.tuned);
    hearForTuning(check.check, check.midi, seconds);
    for (size_t i = 0; i < before.size(); i++){
        if (!before[i] && check.check.strings[i].tuned){
            check.tunedAt[i] = GetTime();
            playPreview(midiToFrequency((float)check.check.tuning[i] + 24.0f)); // a chime: that one's done
        }
    }
    if (check.doneAt < 0.0 && allTuned(check.check)) check.doneAt = GetTime();
}

// Reads what arrived since the last frame and hears the pitch in it, if an open string is sounding
static void listen(){
    const int size = (int)check.window.size();
    float* window = check.window.data();
    int fresh = 0, got;
    while ((got = readCapture(check.incoming.data(), size, check.channel)) > 0){
        std::memmove(window, window + got, (size - got) * sizeof(float));
        std::memcpy(window + size - got, check.incoming.data(), got * sizeof(float));
        fresh += got;
    }
    if (fresh == 0) return;
    float squares = 0.0f;
    for (int i = 0; i < size; i++) squares += window[i] * window[i];
    check.levelDb = 20.0f * std::log10(std::max(std::sqrt(squares / size), 1e-6f));
    float seconds = (float)fresh / std::max(1, captureSampleRate());
    trackNoiseFloor(check.floor, check.levelDb, seconds);
    if (!isSounding(check.floor, check.levelDb)) return;
    PitchResult pitch = detectPitch(check.detector, window, size);
    if (pitch.frequency > 0.0f) hear(frequencyToMidi(pitch.frequency), seconds);
}

static const char* instrumentWord(){
    return check.instrument == InputRole::Bass ? "bass" : "guitar";
}

TuningChoice tuningScreen(){
    TuningChoice choice = TuningChoice::None;
    if (!check.active) return choice;
    listen();

    beginMenu("Tuning");
    const float s = menuScale(), width = ImGui::GetWindowWidth(), height = ImGui::GetWindowHeight();
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const UiFonts& fonts = uiFonts();
    const float left = width * 0.07f;
    menuScreenTitle(TextFormat("Tune your %s", instrumentWord()), s);
    std::string lead = check.reason.empty() ? "Play each open string, and tune it until it turns green." : check.reason;
    draw->AddText(fonts.text, 18 * s, ImVec2(left, height * 0.09f + 52 * s), uiColor(check.reason.empty() ? UiColor::Dim : UiColor::Accent), lead.c_str());

    // A card per string, low to high: its note, a meter of how far off it is, and what to do about it
    const int strings = (int)check.check.tuning.size();
    const float gap = 16 * s, cardWidth = std::min(170 * s, (width * 0.86f - gap * (strings - 1)) / strings), cardHeight = 250 * s;
    float x = (width - (cardWidth * strings + gap * (strings - 1))) / 2, top = height * 0.3f;
    const double now = GetTime();
    const bool hearing = now - check.heardAt < HEARD_HOLD_S;
    const float follow = std::min(1.0f, std::min(GetFrameTime(), 0.05f) * 12.0f);
    for (int i = 0; i < strings; i++, x += cardWidth + gap){
        const StringCheck& string = check.check.strings[i];
        const int note = check.check.tuning[i];
        const bool live = hearing && check.check.lastString == i;
        const bool inTune = string.heard && std::fabs(string.cents) <= IN_TUNE_CENTS;
        check.needle[i] += (std::clamp(string.cents, -SHOWN_CENTS, SHOWN_CENTS) - check.needle[i]) * follow;
        float flash = std::max(0.0f, 1.0f - (float)(now - check.tunedAt[i]) / 0.6f);

        ImVec2 min(x, top), max(x + cardWidth, top + cardHeight);
        draw->AddRectFilled(ImVec2(min.x, min.y + 3 * s), ImVec2(max.x, max.y + 3 * s), uiColor(UiColor::Ink, 0.04f), 12 * s);
        draw->AddRectFilled(min, max, uiColor(UiColor::Card), 12 * s);
        if (string.tuned) draw->AddRectFilled(min, max, uiColor(UiColor::Good, 0.08f + 0.25f * flash), 12 * s);
        UiColor edge = string.tuned ? UiColor::Good : live ? UiColor::Accent : UiColor::StaffLine;
        draw->AddRect(min, max, uiColor(edge, live || string.tuned ? 1.0f : 0.8f), 12 * s, 0, (live ? 2.5f : 1.5f) * s);

        // The note, big, its octave small beside it
        const char* name = pitchClassName(note);
        float nameSize = 72 * s;
        float nameWidth = fonts.heavy ? fonts.heavy->CalcTextSizeA(nameSize, FLT_MAX, 0.0f, name).x : 40 * s;
        float centerX = x + cardWidth / 2;
        UiColor nameColor = string.tuned ? UiColor::Good : (live || !string.heard) ? UiColor::Ink : UiColor::Dim;
        draw->AddText(fonts.heavy, nameSize, ImVec2(centerX - nameWidth / 2 - 6 * s, top + 26 * s), uiColor(nameColor), name);
        draw->AddText(fonts.bold, 20 * s, ImVec2(centerX + nameWidth / 2 - 2 * s, top + 70 * s), uiColor(UiColor::Dim), TextFormat("%d", pitchOctave(note)));

        // The meter: the in-tune zone in the middle, the needle where the string is
        float meterLeft = x + 18 * s, meterRight = x + cardWidth - 18 * s, meterY = top + 150 * s, reach = (meterRight - meterLeft) / 2;
        float mid = (meterLeft + meterRight) / 2, zone = reach * IN_TUNE_CENTS / SHOWN_CENTS;
        draw->AddRectFilled(ImVec2(meterLeft, meterY - 2 * s), ImVec2(meterRight, meterY + 2 * s), uiColor(UiColor::StaffLine), 2 * s);
        draw->AddRectFilled(ImVec2(mid - zone, meterY - 9 * s), ImVec2(mid + zone, meterY + 9 * s), uiColor(UiColor::Good, 0.22f), 3 * s);
        verticalLine(draw, mid, meterY - 12 * s, meterY + 12 * s, 1.5f * s, uiColor(UiColor::Dim, 0.7f));
        if (string.heard){
            float needleX = mid + reach * check.needle[i] / SHOWN_CENTS;
            UiColor needleColor = inTune ? UiColor::Good : UiColor::Accent;
            float alpha = live ? 1.0f : 0.45f;
            verticalLine(draw, needleX, meterY - 14 * s, meterY + 14 * s, 3 * s, uiColor(needleColor, alpha));
            draw->AddCircleFilled(ImVec2(needleX, meterY - 14 * s), 5 * s, uiColor(needleColor, alpha), 16);
        }

        // What it says: play it, which way to turn, or done
        std::string status;
        UiColor statusColor = UiColor::Dim;
        if (!string.heard) status = "Play it";
        else if (string.tuned && (inTune || !live)){ status = "In tune"; statusColor = UiColor::Good; }
        else if (inTune){ status = "Hold it..."; statusColor = UiColor::Good; }
        else {
            status = TextFormat("%+.0f  ·  %s", string.cents, string.cents > 0 ? "tune down" : "tune up");
            statusColor = live ? UiColor::Accent : UiColor::Dim;
        }
        float statusWidth = fonts.bold ? fonts.bold->CalcTextSizeA(17 * s, FLT_MAX, 0.0f, status.c_str()).x : 60 * s;
        draw->AddText(fonts.bold, 17 * s, ImVec2(centerX - statusWidth / 2, top + 190 * s), uiColor(statusColor), status.c_str());
        if (string.tuned){
            // A check mark under it
            ImVec2 tick[3] = { ImVec2(centerX - 8 * s, top + 226 * s), ImVec2(centerX - 2 * s, top + 232 * s), ImVec2(centerX + 9 * s, top + 219 * s) };
            draw->AddPolyline(tick, 3, uiColor(UiColor::Good), ImDrawFlags_None, 2.5f * s);
        }
    }

    // The input: how loud it is, so a silent cable is seen at once
    float meterTop = top + cardHeight + 44 * s, meterWidth = 220 * s, meterX = (width - meterWidth) / 2;
    float fill = std::clamp((check.levelDb + 70.0f) / 70.0f, 0.0f, 1.0f);
    draw->AddRectFilled(ImVec2(meterX, meterTop), ImVec2(meterX + meterWidth, meterTop + 4 * s), uiColor(UiColor::StaffLine), 2 * s);
    draw->AddRectFilled(ImVec2(meterX, meterTop), ImVec2(meterX + meterWidth * fill, meterTop + 4 * s),
                        uiColor(isSounding(check.floor, check.levelDb) ? UiColor::Good : UiColor::Dim), 2 * s);
    std::string input = check.channel >= 0 ? TextFormat("INPUT %d  ·  %s", check.channel + 1, captureDeviceName()) : captureDeviceName();
    float inputWidth = fonts.mono ? fonts.mono->CalcTextSizeA(12 * s, FLT_MAX, 0.0f, input.c_str()).x : 100 * s;
    draw->AddText(fonts.mono, 12 * s, ImVec2(width / 2 - inputWidth / 2, meterTop + 12 * s), uiColor(UiColor::Dim), input.c_str());

    // Done: a moment to see it, then the song
    if (check.doneAt >= 0.0){
        const char* done = "In tune. Here we go!";
        float doneWidth = fonts.bold ? fonts.bold->CalcTextSizeA(22 * s, FLT_MAX, 0.0f, done).x : 200 * s;
        draw->AddText(fonts.bold, 22 * s, ImVec2(width / 2 - doneWidth / 2, top - 52 * s), uiColor(UiColor::Good), done);
        if (now - check.doneAt >= DONE_PAUSE_S) choice = TuningChoice::Tuned;
    }

    // Skip, for a player who knows better (or a tuner of their own): bottom right, with Enter
    bool skip = menuPill("Skip", "Enter", ImVec2(width * 0.93f, height - 40 * s - 8 * s), true, 1, s);
    if (skip || ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter)){
        if (choice == TuningChoice::None) choice = TuningChoice::Skipped;
    }
    menuScreenHint("Enter  skip    Esc  back", s);
    ImGui::End();
    return choice;
}
