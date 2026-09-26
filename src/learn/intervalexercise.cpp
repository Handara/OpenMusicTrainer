#include "learn/intervalexercise.h"

#include "audio/audio.h"
#include "core/music.h"
#include "imgui.h"
#include "raylib.h"
#include "ui/ui.h"

#include <algorithm>
#include <random>

const double AUTO_NEXT_AFTER_S = 1.0;    // after a right answer, the next question plays by itself
const float ANSWER_BUTTON_WIDTH = 170.0f;
const float ANSWER_BUTTON_HEIGHT = 84.0f;
const int ANSWER_COLUMNS = 4;

const ImVec4 RIGHT_COLOR = { 0.25f, 0.62f, 0.32f, 1.0f };
const ImVec4 WRONG_COLOR = { 0.72f, 0.26f, 0.22f, 1.0f };
const ImU32 FEEDBACK_RIGHT = IM_COL32(120, 220, 130, 255);
const ImU32 FEEDBACK_WRONG = IM_COL32(255, 130, 110, 255);
const ImU32 TEXT_DIM = IM_COL32(220, 200, 180, 200);

IntervalExercise::IntervalExercise(const std::string& title, const IntervalConfig& config, const std::string& progressPath)
    : title(title), progressPath(progressPath){
    trainer.config = config;
    trainer.progress = loadIntervalProgress(progressPath);
    trainer.rng.seed(std::random_device{}()); // different questions every session
    // Space and the number keys answer here, so ImGui's keyboard navigation (which also uses them) is off
    ImGui::GetIO().ConfigFlags &= ~ImGuiConfigFlags_NavEnableKeyboard;
    nextQuestion();
}

// The destructor puts back what the constructor changed, so leaving the exercise in any way restores it
IntervalExercise::~IntervalExercise(){
    stopPreviews();
    ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
}

void IntervalExercise::playInterval(int firstPitch, int secondPitch){
    stopPreviews(); // a replay shouldn't pile up on the last one
    playPreview(midiToFrequency((float)firstPitch));
    float gap = trainer.config.direction == IntervalDirection::Harmonic ? 0.0f : trainer.config.gapSeconds;
    playPreview(midiToFrequency((float)secondPitch), gap);
}

void IntervalExercise::nextQuestion(){
    question = nextIntervalQuestion(trainer);
    answered = false;
    playInterval(question.firstPitch, question.secondPitch);
}

void IntervalExercise::submit(int semitones){
    if (answered) return;
    IntervalAnswerResult result = answerInterval(trainer, question, semitones);
    answered = true;
    lastCorrect = result.correct;
    lastAnswer = semitones;
    answeredAt = GetTime();
    newlyUnlocked = result.unlocked;
    sessionAsked++;
    if (result.correct) sessionCorrect++;

    // Saved after every answer: closing the game at any moment loses nothing
    saveError.clear();
    if (!saveIntervalProgress(progressPath, trainer.progress, saveError)) TraceLog(LOG_WARNING, "Progress: %s", saveError.c_str());
}

void IntervalExercise::update(){
    if (ImGui::IsKeyPressed(ImGuiKey_Space) || ImGui::IsKeyPressed(ImGuiKey_Enter)){
        if (answered) nextQuestion();
        else playInterval(question.firstPitch, question.secondPitch);
    }
    // Number keys pick answers in the order the buttons show them: 1 = first button ... 0 = tenth
    std::vector<int> choices = unlockedIntervals(trainer.config, trainer.progress);
    std::sort(choices.begin(), choices.end());
    for (int i = 0; i < (int)choices.size() && i < 10; i++){
        ImGuiKey key = i < 9 ? (ImGuiKey)(ImGuiKey_1 + i) : ImGuiKey_0;
        if (ImGui::IsKeyPressed(key)) submit(choices[i]);
    }
    // A right answer moves on by itself (unless something new was just unlocked: give time to read it)
    if (answered && lastCorrect && newlyUnlocked == 0 && GetTime() - answeredAt > AUTO_NEXT_AFTER_S) nextQuestion();
}

void IntervalExercise::draw(){
    const IntervalProgress& progress = trainer.progress;
    const IntervalConfig& config = trainer.config;
    menuTitle(title.c_str());

    int unlocked = (int)unlockedIntervals(config, progress).size();
    int poolSize = (int)config.pool.size();
    int recentCorrect = (int)std::count(progress.recent.begin(), progress.recent.end(), true);
    int percent = sessionAsked > 0 ? 100 * sessionCorrect / sessionAsked : 0;
    centeredColoredText(TextFormat("%d of %d intervals    This session: %d/%d (%d%%)    Streak %d, best %d",
                                   unlocked, poolSize, sessionCorrect, sessionAsked, percent,
                                   trainer.streak, progress.bestStreak), TEXT_DIM);
    if (unlocked < poolSize){
        centeredColoredText(TextFormat("Next interval unlocks at %d right out of your last %d: now %d",
                                       config.unlockCorrect, config.unlockWindow, recentCorrect), TEXT_DIM);
    }
    ImGui::Dummy(ImVec2(0, 10));
    if (menuButton(answered ? "Next  [Space]" : "Play again  [Space]")){
        if (answered) nextQuestion();
        else playInterval(question.firstPitch, question.secondPitch);
    }
    ImGui::Dummy(ImVec2(0, 10));

    // Answer buttons in size order (easier to find than unlock order), with each interval's accuracy
    std::vector<int> choices = unlockedIntervals(trainer.config, progress);
    std::sort(choices.begin(), choices.end());
    int columns = std::min(ANSWER_COLUMNS, (int)choices.size());
    float rowWidth = columns * ANSWER_BUTTON_WIDTH + (columns - 1) * ImGui::GetStyle().ItemSpacing.x;
    for (int i = 0; i < (int)choices.size(); i++){
        int semitones = choices[i];
        const IntervalInfo& info = intervalInfo(semitones);
        const IntervalStats& stats = progress.stats[semitones];
        std::string label = std::string(info.shortName) + "\n" + info.name;
        if (stats.asked > 0) label += TextFormat("\n%d%%", 100 * stats.correct / stats.asked);
        label += TextFormat("##%d", semitones); // ## hides the rest from display but keeps each button's id unique

        if (i % columns == 0) ImGui::SetCursorPosX((ImGui::GetWindowWidth() - rowWidth) / 2);
        else ImGui::SameLine();

        // After an answer: the right one lights green, a wrong pick red
        int colors = 0;
        if (answered && semitones == question.semitones){ ImGui::PushStyleColor(ImGuiCol_Button, RIGHT_COLOR); colors++; }
        else if (answered && semitones == lastAnswer){ ImGui::PushStyleColor(ImGuiCol_Button, WRONG_COLOR); colors++; }
        if (ImGui::Button(label.c_str(), ImVec2(ANSWER_BUTTON_WIDTH, ANSWER_BUTTON_HEIGHT))) submit(semitones);
        ImGui::PopStyleColor(colors);
    }

    ImGui::Dummy(ImVec2(0, 10));
    if (answered){
        const IntervalInfo& right = intervalInfo(question.semitones);
        if (lastCorrect){
            centeredColoredText(TextFormat("Right! %s", right.name), FEEDBACK_RIGHT);
        } else {
            centeredColoredText(TextFormat("It was a %s. You answered %s.", right.name, intervalInfo(lastAnswer).name), FEEDBACK_WRONG);
            // Hearing both side by side is how the difference gets learned
            float buttonsWidth = 2 * 240.0f + ImGui::GetStyle().ItemSpacing.x;
            ImGui::SetCursorPosX((ImGui::GetWindowWidth() - buttonsWidth) / 2);
            if (ImGui::Button(TextFormat("Hear the %s", right.shortName), ImVec2(240, 44))) playInterval(question.firstPitch, question.secondPitch);
            ImGui::SameLine();
            int yourSecond = question.firstPitch + (question.secondPitch > question.firstPitch ? lastAnswer : -lastAnswer);
            if (trainer.config.direction == IntervalDirection::Harmonic) yourSecond = question.firstPitch + lastAnswer;
            if (ImGui::Button(TextFormat("Hear your %s", intervalInfo(lastAnswer).shortName), ImVec2(240, 44))) playInterval(question.firstPitch, yourSecond);
        }
        if (newlyUnlocked != 0){
            ImGui::Dummy(ImVec2(0, 6));
            centeredColoredText(TextFormat("New interval unlocked: %s!", intervalInfo(newlyUnlocked).name), FEEDBACK_RIGHT);
            ImGui::SetCursorPosX((ImGui::GetWindowWidth() - 240) / 2);
            if (ImGui::Button(TextFormat("Hear the %s", intervalInfo(newlyUnlocked).shortName), ImVec2(240, 44))){
                playInterval(question.firstPitch, question.firstPitch + newlyUnlocked);
            }
        }
    }
    if (!saveError.empty()) centeredErrorText("Progress could not be saved: " + saveError);

    ImGui::Dummy(ImVec2(0, 16));
    if (menuButton("Back")) leave = true;
}
