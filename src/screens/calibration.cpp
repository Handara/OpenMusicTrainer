#include "screens/calibration.h"

#include "audio/audio.h"
#include "core/calibration.h"
#include "input/noteinput.h"
#include "raylib.h"
#include "ui/ui.h"
#include "ui/theme.h"

#include <cmath>
#include <vector>

const double BEAT_S = 0.6;       // 100 BPM: slow enough to be comfortable
const int BEATS = 24;
const int WARMUP_BEATS = 4;      // the first clicks are for finding the pulse, not measured
const double LEAD_IN_S = 1.0;    // silence before the first click
const double LOOKAHEAD_S = 0.2;  // clicks are handed to the audio engine this far ahead
const float LOWEST_EXPECTED_NOTE_HZ = 40.0f;


static struct {
    CalibrationMode mode = CalibrationMode::Tap;
    int globalOffsetMs = 0;
    double start = 0.0;          // audio time of the first click
    int nextClick = 0;           // the next click to schedule
    std::vector<double> differences;
    long long lastBeat = -1;     // the click the last tap or note was counted for: one each
    bool done = false;
    OffsetEstimate estimate;
    bool active = false;
} calibration;

static void restart(){
    calibration.start = audioTime() + LEAD_IN_S;
    calibration.nextClick = 0;
    calibration.differences.clear();
    calibration.lastBeat = -1;
    calibration.done = false;
}

bool startCalibration(CalibrationMode mode, const Settings& settings, std::string& error){
    stopCalibration();
    // The instrument's own input (the guitar's, else the bass's): never a microphone, which would hear the clicks
    // coming out of the speakers and measure those instead of the player
    int channel = settings.guitarChannel >= 0 ? settings.guitarChannel : settings.bassChannel;
    if (mode == CalibrationMode::Instrument && !startNoteInput(settings.inputDevice, LOWEST_EXPECTED_NOTE_HZ, error, channel)) return false;
    calibration.mode = mode;
    calibration.globalOffsetMs = settings.globalOffsetMs;
    ImGui::GetIO().ConfigFlags &= ~ImGuiConfigFlags_NavEnableKeyboard; // Space taps here, it mustn't press buttons
    calibration.active = true;
    restart();
    return true;
}

void stopCalibration(){
    if (!calibration.active) return;
    stopNoteInput();
    ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    calibration.active = false;
}

// A tap or note at audio time t: compared with the click it's nearest to. Only the first for each click counts: a
// pluck can be heard as two notes (its attack bumping twice, a bass note's pitch misread as it rings), and the
// second, always late, would drag the measured delay later.
static void record(double t){
    long long beat = std::llround((t - calibration.start) / BEAT_S);
    if (beat < WARMUP_BEATS || beat >= BEATS || beat == calibration.lastBeat) return;
    calibration.lastBeat = beat;
    calibration.differences.push_back(t - (calibration.start + beat * BEAT_S));
}

static void update(){
    double now = audioTime();
    while (calibration.nextClick < BEATS && calibration.start + calibration.nextClick * BEAT_S < now + LOOKAHEAD_S){
        playClickAt(calibration.start + calibration.nextClick * BEAT_S, calibration.nextClick % 4 == 0);
        calibration.nextClick++;
    }
    if (calibration.mode == CalibrationMode::Tap){
        if (IsKeyPressed(KEY_SPACE)) record(now);
    } else {
        // Each pluck's attack, not its note: a muted string has no pitch but lands on the beat all the same, and the
        // attack is placed to the millisecond
        updateNoteInput();
        for (double age : noteInputAttacks()) record(now - age);
    }
    if (now > calibration.start + BEATS * BEAT_S + 0.4){
        calibration.done = true;
        calibration.estimate = estimateOffset(calibration.differences);
    }
}

// One dot per click: warm-up ones dimmer, the ones already played filled
static void drawBeatDots(){
    int played = (int)std::floor((audioTime() - calibration.start) / BEAT_S) + 1;
    if (calibration.done) played = BEATS;
    const float spacing = 26.0f;
    float left = ImGui::GetWindowPos().x + (ImGui::GetWindowWidth() - spacing * (BEATS - 1)) / 2;
    float y = ImGui::GetCursorScreenPos().y + 12;
    ImDrawList* draw = ImGui::GetWindowDrawList();
    for (int i = 0; i < BEATS; i++){
        ImU32 color = i >= played ? uiColor(UiColor::Dim, 0.3f) : (i < WARMUP_BEATS ? uiColor(UiColor::Accent, 0.45f) : uiColor(UiColor::Accent));
        draw->AddCircleFilled(ImVec2(left + i * spacing, y), i % 4 == 0 ? 8.0f : 6.0f, color);
    }
    ImGui::Dummy(ImVec2(0, 30));
}

CalibrationChoice calibrationScreen(){
    CalibrationChoice choice;
    if (!calibration.done) update();
    bool tap = calibration.mode == CalibrationMode::Tap;

    beginMenu("Calibration");
    menuTitle(tap ? "Calibrate: tapping" : "Calibrate: your instrument");
    centeredText(tap ? "Press Space on every click you hear. Close your eyes: go by ear, not by the screen."
                     : "Play one short note on every click you hear: any note, muted strings are fine.");
    centeredColoredText("The first 4 clicks are to find the pulse; the next 20 are measured.", uiColor(UiColor::Dim));
    ImGui::Dummy(ImVec2(0, 10));
    drawBeatDots();
    centeredText(TextFormat("%s counted: %d", tap ? "Taps" : "Notes", (int)calibration.differences.size()));
    ImGui::Dummy(ImVec2(0, 10));

    if (calibration.done){
        const OffsetEstimate& estimate = calibration.estimate;
        int measuredMs = (int)std::lround(estimate.offset * 1000);
        // Instrument: the measured delay includes the output side, which the global offset already covers
        int offsetMs = tap ? measuredMs : measuredMs - calibration.globalOffsetMs;
        if (estimate.valid){
            centeredText(TextFormat("Measured %+d ms, steady within %d ms, from %d %s",
                                    measuredMs, (int)std::lround(estimate.spread * 1000), estimate.count, tap ? "taps" : "notes"));
            centeredText(TextFormat("New %s offset: %+d ms", tap ? "global" : "input", offsetMs));
            if (estimate.spread > 0.03) centeredColoredText("That's quite uneven: trying again may give a better result.", uiColor(UiColor::Dim));
            if (menuButton("Apply")){
                choice.apply = true;
                choice.offsetMs = offsetMs;
            }
        } else {
            centeredErrorText(TextFormat("Only %d usable %s: at least 8 are needed.", estimate.count, tap ? "taps" : "notes"));
        }
        if (menuButton("Try again")) restart();
    }
    ImGui::End();
    return choice;
}
