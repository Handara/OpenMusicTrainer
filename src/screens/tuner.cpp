#include "screens/tuner.h"

#include "audio/audio.h"
#include "imgui.h"
#include "core/music.h"
#include "core/pitch.h"
#include "raylib.h"
#include "ui/ui.h"
#include "ui/theme.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

const float MIN_FREQUENCY = 30.0f;          // below a 5-string bass's low B (30.9 Hz)
const float MAX_FREQUENCY = 1400.0f;        // above the 24th fret of the high e string (1319 Hz)
const float SILENCE_THRESHOLD_DB = -50.0f;  // quieter than this, don't try to detect a pitch
const float SMOOTHING = 0.25f;              // fraction of the gap to a new reading the needle closes each frame
const float NOTE_CHANGE_SEMITONES = 0.5f;   // a bigger jump is a different note: snap instead of smoothing
const double HOLD_TIME_S = 0.6;             // keep showing the last note this long after the sound stops
const float IN_TUNE_CENTS = 5.0f;           // about the smallest difference most people can hear

const float NOTE_FONT_SIZE = 140.0f;
const float SCALE_WIDTH = 600.0f;
const float SCALE_HEIGHT = 90.0f;

static struct {
    PitchDetector detector;
    std::vector<float> window;   // the most recent samples, oldest first; its size is what YIN needs
    std::vector<float> incoming; // scratch for new samples from the capture buffer
    float levelDb = -100.0f;
    float smoothedMidi = 0.0f;   // fractional MIDI pitch shown on screen
    double lastDetectionTime = -100.0;
    bool active = false;
} tuner;

bool startTuner(const std::string& inputDevice, std::string& error){
    stopTuner();
    if (!startCapture(inputDevice, error)) return false;

    // Everything is sized once here from the device's sample rate, so updateTuner never allocates
    initPitchDetector(tuner.detector, captureSampleRate(), MIN_FREQUENCY, MAX_FREQUENCY);
    int windowSize = pitchWindowSize(tuner.detector);
    tuner.window.assign(windowSize, 0.0f);
    tuner.incoming.assign(windowSize, 0.0f);
    tuner.levelDb = -100.0f;
    tuner.lastDetectionTime = -100.0;
    tuner.active = true;
    TraceLog(LOG_INFO, "Tuner: listening to '%s' at %d Hz, %d-sample window (%.0f ms)",
             captureDeviceName(), captureSampleRate(), windowSize, 1000.0 * windowSize / captureSampleRate());
    return true;
}

void stopTuner(){
    if (!tuner.active) return;
    stopCapture();
    tuner.active = false;
}

void updateTuner(){
    if (!tuner.active) return;

    // Slide the window forward: drop the oldest samples, append everything that arrived since last frame
    const int windowSize = (int)tuner.window.size();
    float* window = tuner.window.data();
    int newSamples = 0;
    int got;
    while ((got = readCapture(tuner.incoming.data(), windowSize)) > 0){
        std::memmove(window, window + got, (windowSize - got) * sizeof(float));
        std::memcpy(window + windowSize - got, tuner.incoming.data(), got * sizeof(float));
        newSamples += got;
    }
    if (newSamples == 0) return;

    // Input level (RMS in decibels relative to full scale): shows the input works, and gates out silence
    float sumSquares = 0.0f;
    for (int i = 0; i < windowSize; i++) sumSquares += window[i] * window[i];
    float rms = std::sqrt(sumSquares / windowSize);
    tuner.levelDb = 20.0f * std::log10(std::max(rms, 1e-6f));
    if (tuner.levelDb < SILENCE_THRESHOLD_DB) return;

    PitchResult pitch = detectPitch(tuner.detector, window, windowSize);
    if (pitch.frequency <= 0.0f) return;

    float midi = frequencyToMidi(pitch.frequency);
    bool showingNote = GetTime() - tuner.lastDetectionTime < HOLD_TIME_S;
    if (!showingNote || std::fabs(midi - tuner.smoothedMidi) > NOTE_CHANGE_SEMITONES){
        tuner.smoothedMidi = midi;
    } else {
        tuner.smoothedMidi += (midi - tuner.smoothedMidi) * SMOOTHING;
    }
    tuner.lastDetectionTime = GetTime();
}

void drawTuner(){
    bool showingNote = GetTime() - tuner.lastDetectionTime < HOLD_TIME_S;
    int nearestNote = (int)std::lround(tuner.smoothedMidi);
    float cents = (tuner.smoothedMidi - nearestNote) * 100.0f;
    ImU32 color = !showingNote ? uiColor(UiColor::Dim) : std::fabs(cents) <= IN_TUNE_CENTS ? uiColor(UiColor::Good) : uiColor(UiColor::Accent);

    // Note name and details
    ImGui::PushFont(nullptr, NOTE_FONT_SIZE);
    centeredColoredText(showingNote ? TextFormat("%s%d", pitchClassName(nearestNote), pitchOctave(nearestNote)) : "--",
                        color);
    ImGui::PopFont();
    centeredColoredText(showingNote ? TextFormat("%.1f Hz    %+.0f cents", midiToFrequency(tuner.smoothedMidi), cents)
                                    : "Play a note", uiColor(UiColor::Dim, 0.8f));

    // Cents scale from -50 (a quarter tone flat) to +50 (a quarter tone sharp), drawn with ImGui's draw list
    ImDrawList* draw = ImGui::GetWindowDrawList();
    float left = ImGui::GetWindowPos().x + (ImGui::GetWindowWidth() - SCALE_WIDTH) / 2;
    float centerX = left + SCALE_WIDTH / 2;
    float top = ImGui::GetCursorScreenPos().y + 30;
    float bottom = top + SCALE_HEIGHT;
    auto centsToX = [&](float c){ return centerX + std::clamp(c, -50.0f, 50.0f) / 50.0f * (SCALE_WIDTH / 2); };

    draw->AddRectFilled(ImVec2(centsToX(-IN_TUNE_CENTS), top), ImVec2(centsToX(IN_TUNE_CENTS), bottom),
                        uiColor(UiColor::Good, 0.2f), 4.0f);
    for (int c = -50; c <= 50; c += 10){
        float tickHeight = c == 0 ? SCALE_HEIGHT : (c % 50 == 0 ? SCALE_HEIGHT * 0.6f : SCALE_HEIGHT * 0.35f);
        draw->AddLine(ImVec2(centsToX((float)c), bottom - tickHeight), ImVec2(centsToX((float)c), bottom),
                      uiColor(UiColor::Dim, 0.8f), c == 0 ? 3.0f : 1.5f);
    }
    draw->AddText(ImVec2(left - 20, bottom + 8), uiColor(UiColor::Dim, 0.8f), "-50");
    draw->AddText(ImVec2(left + SCALE_WIDTH - 20, bottom + 8), uiColor(UiColor::Dim, 0.8f), "+50");
    if (showingNote){
        float needleX = centsToX(cents);
        draw->AddLine(ImVec2(needleX, top - 12), ImVec2(needleX, bottom + 4), color, 5.0f);
        draw->AddCircleFilled(ImVec2(needleX, top - 12), 8.0f, color);
    }
    ImGui::Dummy(ImVec2(0, SCALE_HEIGHT + 90)); // tell ImGui how much space the drawing used

    // Input level meter from -60 dB (silence) to 0 dB (the loudest the input can go), with the silence gate marked
    float meterTop = ImGui::GetCursorScreenPos().y;
    float fill = std::clamp((tuner.levelDb + 60.0f) / 60.0f, 0.0f, 1.0f);
    float gateX = left + (SILENCE_THRESHOLD_DB + 60.0f) / 60.0f * SCALE_WIDTH;
    draw->AddRectFilled(ImVec2(left, meterTop), ImVec2(left + SCALE_WIDTH, meterTop + 12), uiColor(UiColor::StaffLine), 3.0f);
    draw->AddRectFilled(ImVec2(left, meterTop), ImVec2(left + fill * SCALE_WIDTH, meterTop + 12), uiColor(UiColor::Good), 3.0f);
    draw->AddLine(ImVec2(gateX, meterTop - 4), ImVec2(gateX, meterTop + 16), uiColor(UiColor::Dim, 0.8f), 2.0f);
    ImGui::Dummy(ImVec2(0, 24));
    centeredColoredText(TextFormat("Input: %s    %.0f dB", captureDeviceName(), tuner.levelDb), uiColor(UiColor::Dim, 0.8f));
}
