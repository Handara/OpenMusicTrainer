#include "learn/singingexercise.h"

#include "audio/audio.h"
#include "core/music.h"
#include "imgui.h"
#include "raylib.h"
#include "ui/menulist.h"
#include "ui/theme.h"
#include "ui/ui.h"

#include <algorithm>
#include <cmath>
#include <cstring>

const float MIN_FREQUENCY = 60.0f;          // below a low bass voice
const float MAX_FREQUENCY = 1200.0f;        // above a high soprano
const float SILENCE_THRESHOLD_DB = -45.0f;  // quieter than this isn't singing
const double NOTE_SOUNDS_FOR_S = 1.5;       // the played note rings this long: the microphone would hear it
const double AUTO_NEXT_AFTER_S = 1.2;

SingingExercise::SingingExercise(const std::string& title, const SingingConfig& config, const std::string& progressPath,
                                 const std::string& inputDevice)
    : title(title), config(config), progressPath(progressPath){
    progress = loadQuizProgress(progressPath);
    rng.seed(std::random_device{}());
    if (startCapture(inputDevice, inputError)){
        initPitchDetector(detector, captureSampleRate(), MIN_FREQUENCY, MAX_FREQUENCY);
        window.assign(pitchWindowSize(detector), 0.0f);
        incoming.assign(window.size(), 0.0f);
    }
    ImGui::GetIO().ConfigFlags &= ~ImGuiConfigFlags_NavEnableKeyboard; // Space and S work here
    nextNote();
}

SingingExercise::~SingingExercise(){
    stopPreviews();
    stopCapture();
    ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
}

void SingingExercise::nextNote(){
    target = nextSingingNote(config, rng, target);
    answered = false;
    hold = {};
    playNote();
}

void SingingExercise::playNote(){
    stopPreviews();
    playPreview(midiToFrequency((float)target));
    listenFrom = GetTime() + NOTE_SOUNDS_FOR_S;
    hold = {};
}

void SingingExercise::answer(bool right){
    if (answered) return;
    answered = true;
    lastRight = right;
    answeredAt = GetTime();
    recordQuizAnswer(progress, streak, right);
    sessionAsked++;
    if (right) sessionCorrect++;
    saveError.clear();
    if (!saveQuizProgress(progressPath, progress, saveError)) TraceLog(LOG_WARNING, "Progress: %s", saveError.c_str());
}

void SingingExercise::update(){
    // The input, like the tuner's: slide the window along, measure the level, find the pitch if it's loud enough
    if (inputError.empty() && !window.empty()){
        const int size = (int)window.size();
        int got, fresh = 0;
        while ((got = readCapture(incoming.data(), size)) > 0){
            std::memmove(window.data(), window.data() + got, (size - got) * sizeof(float));
            std::memcpy(window.data() + size - got, incoming.data(), got * sizeof(float));
            fresh += got;
        }
        if (fresh > 0){
            float sum = 0.0f;
            for (float sample : window) sum += sample * sample;
            levelDb = 20.0f * std::log10(std::max(std::sqrt(sum / size), 1e-6f));
            PitchResult pitch = levelDb > SILENCE_THRESHOLD_DB ? detectPitch(detector, window.data(), size) : PitchResult{0.0f, 0.0f};
            sungMidi = pitch.frequency > 0.0f ? frequencyToMidi(pitch.frequency) : -1.0f;
        }
    }
    bool listening = GetTime() >= listenFrom;
    if (!answered && listening && holdPitch(hold, config, target, sungMidi, GetFrameTime())) answer(true);

    if (ImGui::IsKeyPressed(ImGuiKey_Space)){
        if (answered) nextNote();
        else playNote();
    }
    if (!answered && ImGui::IsKeyPressed(ImGuiKey_S)) answer(false); // skipped: a miss, and the note is shown
    if (answered && lastRight && GetTime() - answeredAt > AUTO_NEXT_AFTER_S) nextNote();
}

void SingingExercise::draw(){
    menuTitle(title.c_str());
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const UiFonts& fonts = uiFonts();
    float s = menuScale(), width = ImGui::GetWindowWidth();
    float left = width * 0.07f + 40 * s;
    ImVec2 at = ImGui::GetCursorScreenPos();
    at.x = left;

    // The note, big, and what to do
    const char* note = TextFormat("%s%d", pitchClassName(target), pitchOctave(target));
    draw->AddText(fonts.heavy, 64 * s, at, uiColor(UiColor::Ink), note);
    float noteWidth = fonts.heavy ? fonts.heavy->CalcTextSizeA(64 * s, FLT_MAX, 0.0f, note).x : 80 * s;
    bool listening = GetTime() >= listenFrom;
    const char* doing = answered ? "" : (listening ? "sing it, and hold it" : "listen...");
    draw->AddText(fonts.bold, 26 * s, ImVec2(at.x + noteWidth + 16 * s, at.y + 30 * s), uiColor(UiColor::Dim), doing);
    int percent = sessionAsked > 0 ? 100 * sessionCorrect / sessionAsked : 0;
    draw->AddText(fonts.mono, 13 * s, ImVec2(at.x, at.y + 84 * s), uiColor(UiColor::Dim),
                  TextFormat("STREAK %d  ·  BEST %d  ·  THIS SESSION %d/%d (%d%%)", streak, progress.bestStreak,
                             sessionCorrect, sessionAsked, percent));

    // The meter: 50 cents flat to 50 sharp, the in-tune band green, the voice a needle
    float meterTop = at.y + 130 * s, meterWidth = 600 * s, meterHeight = 70 * s;
    auto centsX = [&](float cents){ return left + (std::clamp(cents, -50.0f, 50.0f) + 50.0f) / 100.0f * meterWidth; };
    draw->AddRectFilled(ImVec2(left, meterTop), ImVec2(left + meterWidth, meterTop + meterHeight), uiColor(UiColor::Card), 8 * s);
    draw->AddRectFilled(ImVec2(centsX(-config.toleranceCents), meterTop), ImVec2(centsX(config.toleranceCents), meterTop + meterHeight),
                        uiColor(UiColor::Good, 0.18f));
    verticalLine(draw, centsX(0.0f), meterTop, meterTop + meterHeight, 2.0f * s, uiColor(UiColor::Dim));
    draw->AddText(fonts.mono, 13 * s, ImVec2(left, meterTop + meterHeight + 6 * s), uiColor(UiColor::Dim), "flat");
    draw->AddText(fonts.mono, 13 * s, ImVec2(left + meterWidth - 40 * s, meterTop + meterHeight + 6 * s), uiColor(UiColor::Dim), "sharp");
    float cents = sungMidi >= 0.0f ? singingErrorCents(config, target, sungMidi) : 0.0f;
    if (listening && sungMidi >= 0.0f){
        bool inTune = std::fabs(cents) <= config.toleranceCents;
        verticalLine(draw, centsX(cents), meterTop - 8 * s, meterTop + meterHeight + 8 * s, 5.0f * s,
                     uiColor(inTune ? UiColor::Good : UiColor::Accent));
    }
    // How long it's been held, filling toward the goal
    float holdTop = meterTop + meterHeight + 34 * s;
    float filled = (float)std::min(1.0, hold.inTuneFor / config.holdSeconds);
    draw->AddRectFilled(ImVec2(left, holdTop), ImVec2(left + meterWidth, holdTop + 8 * s), uiColor(UiColor::StaffLine), 4 * s);
    if (answered && lastRight) filled = 1.0f;
    if (filled > 0.0f){ // an empty rounded bar would still show as a dot
        draw->AddRectFilled(ImVec2(left, holdTop), ImVec2(left + meterWidth * filled, holdTop + 8 * s), uiColor(UiColor::Good), 4 * s);
    }

    // What was heard
    float textY = holdTop + 30 * s;
    auto line = [&](UiColor color, const std::string& text){
        draw->AddText(fonts.text, 18 * s, ImVec2(left, textY), uiColor(color), text.c_str());
        textY += 26 * s;
    };
    if (answered) line(lastRight ? UiColor::Good : UiColor::Bad, lastRight ? "Right!" : "Skipped. Space for the next.");
    else if (listening && sungMidi >= 0.0f){
        line(UiColor::Dim, TextFormat("You're singing %s%d, %+.0f cents", pitchClassName((int)std::lround(sungMidi)),
                                      pitchOctave((int)std::lround(sungMidi)), cents));
    } else if (listening) line(UiColor::Dim, "Listening...");
    if (!inputError.empty()) line(UiColor::Bad, "No input device: " + inputError);
    if (!saveError.empty()) line(UiColor::Bad, "Progress could not be saved: " + saveError);
    ImGui::SetCursorScreenPos(ImVec2(left, textY + 10 * s));
    if (ImGui::Button("Back")) leave = true;
    menuScreenHint("Space  hear it again    S  skip    Esc  back", s);
}
