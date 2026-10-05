#include "learn/fretboardexercise.h"

#include "audio/audio.h"
#include "core/music.h"
#include "input/noteinput.h"
#include "imgui.h"
#include "raylib.h"
#include "ui/fretboardview.h"
#include "ui/menulist.h"
#include "ui/scoreboard.h"
#include "ui/theme.h"
#include "ui/ui.h"

#include <algorithm>
#include <random>

const double AUTO_NEXT_AFTER_S = 1.0;  // after a right answer, the next question comes by itself...
const double AUTO_NEXT_WRONG_S = 2.0;  // ...and after a wrong one too, a little later: time to see where it was
const double SOUND_TAIL_S = 1.2;       // playing, the answer's own sound is ignored this long

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
    if (answered && GetTime() - answeredAt > (lastRight ? AUTO_NEXT_AFTER_S : AUTO_NEXT_WRONG_S)) nextQuestion();
}

// Strings across (the highest on top, as in tab), frets down; the asked string in brass. Clicking a fret on it
// answers. After an answer: the right fret(s) in green, a wrong click in red.
void FretboardExercise::drawBoard(float left, float top, float width, float s){
    const FretboardConfig& config = trainer.config;
    FretboardLayout board = fretboardLayout(left, top, width, s, (int)config.tuning.size(), config.lowestFret, config.highestFret);
    drawFretboard(board, config.tuning, question.stringIndex);

    // The mouse over the asked string: a ghost dot where the click would go, and the click answers
    ImVec2 mouse = ImGui::GetMousePos();
    int hoverFret = -1;
    if (!answered && std::abs(mouse.y - board.stringY(question.stringIndex)) < board.spacing / 2) hoverFret = board.fretAt(mouse.x);
    if (hoverFret >= 0){
        drawFretDot(board, question.stringIndex, hoverFret, 11 * s, uiColor(UiColor::Accent, 0.35f), 0, nullptr);
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) answer(fretIsRight(config, question, hoverFret), hoverFret);
    }

    if (answered){
        auto dot = [&](int fret, UiColor color){
            const char* name = pitchClassName(config.tuning[question.stringIndex] + fret);
            drawFretDot(board, question.stringIndex, fret, 12 * s, uiColor(color), uiColor(UiColor::Card), name);
        };
        if (clickedFret >= 0 && !lastRight) dot(clickedFret, UiColor::Bad);
        for (int fret : fretboardAnswers(config, question)) dot(fret, UiColor::Good);
    }
    ImGui::SetCursorScreenPos(ImVec2(left, top + board.height + 34 * s));
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
    drawScoreboard({
        { "STREAK", std::to_string(trainer.streak), trainer.streak > 0 ? UiColor::Good : UiColor::Ink, TextFormat("best %d", trainer.progress.bestStreak) },
        { "THIS SESSION", TextFormat("%d/%d", sessionCorrect, sessionAsked), UiColor::Ink, TextFormat("%d%% right", percent) },
    }, width * 0.93f, ImGui::GetWindowHeight() * 0.03f + 36 * s, s);

    drawBoard(left, at.y + 118 * s, boardWidth, s);

    // What happened, and how to go on
    ImGui::SetCursorPosX(left);
    if (answered){
        if (lastRight) ImGui::TextColored(uiColorVec(UiColor::Good), "Right!");
        else {
            // An open string and its 12th fret are both right: two green dots
            const char* green = fretboardAnswers(config, question).size() > 1 ? "the green ones" : "the green one";
            if (playedPitch >= 0) ImGui::TextColored(uiColorVec(UiColor::Bad), "You played %s%d: it's %s.",
                                                     pitchClassName(playedPitch), pitchOctave(playedPitch), green);
            else ImGui::TextColored(uiColorVec(UiColor::Bad), "It's %s.", green);
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
