#include "learn/routineexercise.h"

#include "imgui.h"
#include "raylib.h"
#include "ui/ui.h"
#include "ui/theme.h"

#include <algorithm>
#include <cmath>

const float BAR_MARGIN = 20.0f;

RoutineExercise::RoutineExercise(const std::string& title, std::vector<Step> steps, const std::string& progressPath, ExerciseFactory create)
    : title(title), steps(std::move(steps)), progressPath(progressPath), create(create){
    progress = loadRoutineProgress(progressPath);
    startStep(0);
}

void RoutineExercise::startStep(int index){
    current = index;
    // The old step goes first: its destructor stops the microphone and turns keyboard navigation back on,
    // which would undo what the new step's constructor just set up if it ran after it
    exercise.reset();
    exercise = create(steps[current].entry);
    stepStartedAt = GetTime();
}

void RoutineExercise::nextStep(){
    practicedSeconds += GetTime() - stepStartedAt;
    if (current + 1 < (int)steps.size()){
        startStep(current + 1);
        return;
    }
    exercise.reset();
    done = true;
    finishRoutine(progress, today());
    if (!saveRoutineProgress(progressPath, progress, saveError)) TraceLog(LOG_WARNING, "Progress: %s", saveError.c_str());
}

void RoutineExercise::update(){
    if (!exercise) return;
    exercise->update();
    if (exercise->wantsToLeave()) leave = true; // the step's Back leaves the whole routine
}

void RoutineExercise::draw(){
    if (done){
        drawDone();
        return;
    }
    exercise->draw();
    drawStepBar();
}

// One line in the top-right corner, in the margin above the step's own title:
// "Step 2 of 3: E minor, open position   2:31 left   [Next step]"
void RoutineExercise::drawStepBar(){
    double left = steps[current].seconds - (GetTime() - stepStartedAt);
    bool timeUp = left <= 0.0;
    int wholeSeconds = (int)std::ceil(std::max(0.0, left));
    std::string text = TextFormat("Step %d of %d: %s    ", current + 1, (int)steps.size(), steps[current].entry.exercise.title.c_str());
    text += timeUp ? "Time's up" : TextFormat("%d:%02d left", wholeSeconds / 60, wholeSeconds % 60);
    const char* label = current + 1 < (int)steps.size() ? "Next step" : "Finish";

    ImGuiStyle& style = ImGui::GetStyle();
    float buttonWidth = ImGui::CalcTextSize(label).x + 2 * style.FramePadding.x;
    float width = ImGui::CalcTextSize(text.c_str()).x + style.ItemSpacing.x + buttonWidth;
    ImGui::SetCursorPos(ImVec2(ImGui::GetWindowWidth() - width - BAR_MARGIN, BAR_MARGIN));
    ImGui::AlignTextToFramePadding(); // the text sits level with the button's label
    ImGui::PushStyleColor(ImGuiCol_Text, timeUp ? uiColor(UiColor::Good) : uiColor(UiColor::Dim));
    ImGui::TextUnformatted(text.c_str());
    ImGui::PopStyleColor();
    ImGui::SameLine();
    if (timeUp){
        ImGui::PushStyleColor(ImGuiCol_Button, uiColorVec(UiColor::Good));
        ImGui::PushStyleColor(ImGuiCol_Text, uiColorVec(UiColor::Card)); // light text on the filled button
    }
    if (ImGui::Button(label)) nextStep();
    if (timeUp) ImGui::PopStyleColor(2);
}

void RoutineExercise::drawDone(){
    menuTitle(title.c_str());
    centeredColoredText("Routine done!", uiColor(UiColor::Good));
    int seconds = (int)std::round(practicedSeconds);
    centeredText(TextFormat("Practiced for %d:%02d", seconds / 60, seconds % 60));
    int streak = currentStreak(progress, today());
    centeredText(TextFormat("Day streak: %d %s    Best: %d    Done %d %s in all", streak, streak == 1 ? "day" : "days",
                            progress.bestStreak, progress.completed, progress.completed == 1 ? "time" : "times"));
    if (!saveError.empty()) centeredErrorText("Progress could not be saved: " + saveError);
    ImGui::Dummy(ImVec2(0, 20));
    if (menuButton("Back")) leave = true;
}
