#include "learn/fretboardexercise.h"

#include "audio/audio.h"
#include "core/music.h"
#include "input/noteinput.h"
#include "imgui.h"
#include "raylib.h"
#include "ui/menulist.h"
#include "ui/theme.h"
#include "ui/ui.h"

#include <algorithm>
#include <random>

const double AUTO_NEXT_AFTER_S = 1.0;  // after a right answer, the next question comes by itself
const double SOUND_TAIL_S = 1.2;       // playing, the answer's own sound is ignored this long
const float STRING_SPACING = 30.0f;    // at a 720-pixel-tall window
const float OPEN_COLUMN = 44.0f;       // room left of the nut for open strings
const int SINGLE_DOTS[] = { 3, 5, 7, 9, 15, 17, 19, 21 }; // the fret markers on a guitar's neck; 12 and 24 get two

FretboardExercise::FretboardExercise(const std::string& title, const FretboardConfig& config, const std::string& progressPath,
                                     const std::string& inputDevice, int channel)
    : title(title), progressPath(progressPath), inputDevice(inputDevice), channel(channel){
    trainer.config = config;
    trainer.progress = loadQuizProgress(progressPath);
    trainer.rng.seed(std::random_device{}());
    ImGui::GetIO().ConfigFlags &= ~ImGuiConfigFlags_NavEnableKeyboard; // Space and Enter move on here
    nextQuestion();
}

FretboardExercise::~FretboardExercise(){
    stopPreviews();
    stopNoteInput();
    ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
}

void FretboardExercise::nextQuestion(){
    question = nextFretboardQuestion(trainer);
    answered = false;
    clickedFret = -1;
    playedPitch = -1;
}

void FretboardExercise::answer(bool right, int fret){
    if (answered) return;
    answered = true;
    lastRight = right;
    clickedFret = fret;
    answeredAt = GetTime();
    recordQuizAnswer(trainer.progress, trainer.streak, right);
    sessionAsked++;
    if (right) sessionCorrect++;
    // The right note sounds either way: hearing where it is helps remember it
    std::vector<int> answers = fretboardAnswers(trainer.config, question);
    playPreview(midiToFrequency((float)(trainer.config.tuning[question.stringIndex] + answers.front())));
    listenFrom = GetTime() + SOUND_TAIL_S;

    saveError.clear();
    if (!saveQuizProgress(progressPath, trainer.progress, saveError)) TraceLog(LOG_WARNING, "Progress: %s", saveError.c_str());
}

void FretboardExercise::setAnswerByPlaying(bool on){
    inputError.clear();
    float lowest = midiToFrequency((float)*std::min_element(trainer.config.tuning.begin(), trainer.config.tuning.end())) * 0.9f;
    if (on && !startNoteInput(inputDevice, lowest, inputError, channel)) on = false;
    if (!on) stopNoteInput();
    byPlaying = on;
}

void FretboardExercise::update(){
    if (byPlaying){
        for (const PlayedNote& note : updateNoteInput()){
            if (answered || GetTime() - note.age < listenFrom) continue;
            playedPitch = note.pitch;
            answer(pitchIsRight(trainer.config, question, note.pitch), -1);
        }
    }
    if (answered && (ImGui::IsKeyPressed(ImGuiKey_Space) || ImGui::IsKeyPressed(ImGuiKey_Enter))) nextQuestion();
    if (answered && lastRight && GetTime() - answeredAt > AUTO_NEXT_AFTER_S) nextQuestion();
}

// Strings across (the highest on top, as in tab), frets down; the asked string in brass. Clicking a fret on it
// answers. After an answer: the right fret(s) in green, a wrong click in red.
void FretboardExercise::drawFretboard(float left, float top, float width, float s){
    const FretboardConfig& config = trainer.config;
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const UiFonts& fonts = uiFonts();
    const int strings = (int)config.tuning.size();
    const int firstFret = config.lowestFret, lastFret = config.highestFret;
    const bool hasOpen = firstFret == 0;
    const float spacing = STRING_SPACING * s, pad = 22 * s;
    const float boardLeft = left + (hasOpen ? OPEN_COLUMN * s : 0.0f), boardRight = left + width;
    const int fretCount = lastFret - std::max(firstFret, 1) + 1; // the fretted columns shown
    const float fretWidth = (boardRight - boardLeft) / std::max(1, fretCount);
    const float height = spacing * (strings - 1) + 2 * pad;
    auto stringY = [&](int string){ return top + pad + (strings - 1 - string) * spacing; };
    // The middle of a fret's column: frets are pressed just behind the fret wire, not on it
    auto fretX = [&](int fret){
        if (fret == 0) return left + OPEN_COLUMN * s / 2;
        return boardLeft + (fret - std::max(firstFret, 1) + 0.5f) * fretWidth;
    };

    draw->AddRectFilled(ImVec2(left, top), ImVec2(boardRight, top + height), uiColor(UiColor::Card), 10 * s);
    // Markers between the strings, then the frets, the nut, the strings
    float middle = top + height / 2;
    for (int fret = std::max(firstFret, 1); fret <= lastFret; fret++){
        bool single = std::count(std::begin(SINGLE_DOTS), std::end(SINGLE_DOTS), fret) > 0;
        if (single) draw->AddCircleFilled(ImVec2(fretX(fret), middle), 6 * s, uiColor(UiColor::StaffLine));
        if (fret == 12 || fret == 24){
            draw->AddCircleFilled(ImVec2(fretX(fret), middle - spacing), 6 * s, uiColor(UiColor::StaffLine));
            draw->AddCircleFilled(ImVec2(fretX(fret), middle + spacing), 6 * s, uiColor(UiColor::StaffLine));
        }
        float wireX = boardLeft + (fret - std::max(firstFret, 1) + 1) * fretWidth;
        if (fret < lastFret) verticalLine(draw, wireX, top + pad * 0.5f, top + height - pad * 0.5f, 1.5f * s, uiColor(UiColor::Dim, 0.5f));
        const char* number = TextFormat("%d", fret);
        float numberWidth = fonts.mono ? fonts.mono->CalcTextSizeA(13 * s, FLT_MAX, 0.0f, number).x : 0.0f;
        draw->AddText(fonts.mono, 13 * s, ImVec2(fretX(fret) - numberWidth / 2, top + height + 6 * s), uiColor(UiColor::Dim), number);
    }
    verticalLine(draw, boardLeft, top + pad * 0.5f, top + height - pad * 0.5f, (hasOpen ? 4.0f : 1.5f) * s, uiColor(UiColor::Ink));
    for (int string = 0; string < strings; string++){
        bool asked = string == question.stringIndex;
        horizontalLine(draw, left + 6 * s, boardRight - 6 * s, stringY(string), (asked ? 3.0f : 1.0f + 0.25f * (strings - 1 - string)) * s,
               uiColor(asked ? UiColor::Accent : UiColor::Ink, asked ? 1.0f : 0.55f));
        // The string's name, left of the board
        const char* name = TextFormat("%s", pitchClassName(config.tuning[string]));
        float nameWidth = fonts.bold ? fonts.bold->CalcTextSizeA(15 * s, FLT_MAX, 0.0f, name).x : 0.0f;
        draw->AddText(fonts.bold, 15 * s, ImVec2(left - nameWidth - 12 * s, stringY(string) - 8 * s),
                      uiColor(asked ? UiColor::Accent : UiColor::Dim), name);
    }

    // The mouse over the asked string: a ghost dot where the click would go, and the click answers
    ImVec2 mouse = ImGui::GetMousePos();
    int hoverFret = -1;
    if (!answered && std::abs(mouse.y - stringY(question.stringIndex)) < spacing / 2 && mouse.x >= left && mouse.x < boardRight){
        if (hasOpen && mouse.x < boardLeft) hoverFret = 0;
        else if (mouse.x >= boardLeft) hoverFret = std::min(lastFret, std::max(firstFret, 1) + (int)((mouse.x - boardLeft) / fretWidth));
    }
    if (hoverFret >= 0){
        draw->AddCircleFilled(ImVec2(fretX(hoverFret), stringY(question.stringIndex)), 11 * s, uiColor(UiColor::Accent, 0.35f));
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) answer(fretIsRight(config, question, hoverFret), hoverFret);
    }

    if (answered){
        auto dot = [&](int fret, UiColor color){
            ImVec2 at(fretX(fret), stringY(question.stringIndex));
            draw->AddCircleFilled(at, 12 * s, uiColor(color));
            const char* name = pitchClassName(config.tuning[question.stringIndex] + fret);
            ImVec2 size = fonts.bold ? fonts.bold->CalcTextSizeA(13 * s, FLT_MAX, 0.0f, name) : ImVec2(0, 0);
            draw->AddText(fonts.bold, 13 * s, ImVec2(at.x - size.x / 2, at.y - size.y / 2), uiColor(UiColor::Card), name);
        };
        if (clickedFret >= 0 && !lastRight) dot(clickedFret, UiColor::Bad);
        for (int fret : fretboardAnswers(config, question)) dot(fret, UiColor::Good);
    }
    ImGui::SetCursorScreenPos(ImVec2(left, top + height + 34 * s));
}

void FretboardExercise::draw(){
    const FretboardConfig& config = trainer.config;
    const UiFonts& fonts = uiFonts();
    ImDrawList* draw = ImGui::GetWindowDrawList();
    menuTitle(title.c_str());
    float s = menuScale(), width = ImGui::GetWindowWidth();
    float left = width * 0.07f + 40 * s, boardWidth = width * 0.86f - 40 * s;

    // The question, big: the note, then its string
    ImVec2 at = ImGui::GetCursorScreenPos();
    at.x = left;
    const char* note = pitchClassName(question.pitchClass);
    draw->AddText(fonts.heavy, 64 * s, at, uiColor(UiColor::Ink), note);
    float noteWidth = fonts.heavy ? fonts.heavy->CalcTextSizeA(64 * s, FLT_MAX, 0.0f, note).x : 40 * s;
    std::string where = std::string("on the ") + pitchClassName(config.tuning[question.stringIndex]) + " string";
    draw->AddText(fonts.bold, 26 * s, ImVec2(at.x + noteWidth + 16 * s, at.y + 30 * s), uiColor(UiColor::Dim), where.c_str());
    int percent = sessionAsked > 0 ? 100 * sessionCorrect / sessionAsked : 0;
    const char* stats = TextFormat("STREAK %d  ·  BEST %d  ·  THIS SESSION %d/%d (%d%%)", trainer.streak, trainer.progress.bestStreak,
                                   sessionCorrect, sessionAsked, percent);
    draw->AddText(fonts.mono, 13 * s, ImVec2(at.x, at.y + 84 * s), uiColor(UiColor::Dim), stats);

    drawFretboard(left, at.y + 118 * s, boardWidth, s);

    // What happened, and how to go on
    ImGui::SetCursorPosX(left);
    if (answered){
        if (lastRight) ImGui::TextColored(uiColorVec(UiColor::Good), "Right!");
        else {
            // An open string and its 12th fret are both right: two green dots
            const char* green = fretboardAnswers(config, question).size() > 1 ? "the green ones" : "the green one";
            if (playedPitch >= 0) ImGui::TextColored(uiColorVec(UiColor::Bad), "You played %s%d: it's %s. Space for the next.",
                                                     pitchClassName(playedPitch), pitchOctave(playedPitch), green);
            else ImGui::TextColored(uiColorVec(UiColor::Bad), "It's %s. Space for the next.", green);
        }
    } else {
        ImGui::TextColored(uiColorVec(UiColor::Dim), byPlaying ? "Play it, or click its fret" : "Click its fret");
    }
    ImGui::SetCursorPosX(left);
    bool playing = byPlaying;
    if (ImGui::Checkbox("Answer by playing", &playing)) setAnswerByPlaying(playing);
    if (!inputError.empty()){
        ImGui::SetCursorPosX(left);
        ImGui::TextColored(uiColorVec(UiColor::Bad), "No input device: %s", inputError.c_str());
    }
    if (!saveError.empty()){
        ImGui::SetCursorPosX(left);
        ImGui::TextColored(uiColorVec(UiColor::Bad), "Progress could not be saved: %s", saveError.c_str());
    }
    ImGui::SetCursorPosX(left);
    if (ImGui::Button("Back")) leave = true;
    menuScreenHint("Click or play the note    Space  next    Esc  back", s);
}
