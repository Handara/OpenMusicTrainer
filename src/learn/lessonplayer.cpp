#include "learn/lessonplayer.h"

#include "imgui.h"
#include "learn/playexercise.h"
#include "raylib.h"
#include "ui/ui.h"
#include "ui/theme.h"

#include <algorithm>
#include <filesystem>

const float MAX_STEP_WIDTH = 900.0f;
const float DOT_RADIUS = 6.0f;
const float DOT_SPACING = 22.0f;
const float BAR_MARGIN = 20.0f;

LessonPlayer::LessonPlayer(const LessonEntry& entry, std::vector<ExerciseEntry> stepExercises, ExerciseFactory create,
                           const GameplayOptions& playOptions, const std::string& progressPath)
    : lesson(entry.lesson), folder(entry.folder), stepExercises(std::move(stepExercises)), create(create),
      playOptions(playOptions), progressPath(progressPath){
    progress = loadLessonProgress(progressPath);
    // Reopens where the student was; a finished lesson starts over from the top
    current = progress.completed ? 0 : std::clamp(progress.reached, 0, (int)lesson.steps.size() - 1);
}

LessonPlayer::~LessonPlayer(){
    running.reset();
    releaseLessonMedia(media);
}

bool LessonPlayer::hasGoal() const {
    return step().type == LessonStepType::Exercise || step().type == LessonStepType::Play;
}

int LessonPlayer::goal() const {
    const ExerciseEntry& exercise = stepExercises[current];
    return lessonGoal(step(), exercise.exercise.type);
}

bool LessonPlayer::canGoOn() const {
    return !hasGoal() || stepPassed(progress, current);
}

void LessonPlayer::save(){
    saveError.clear();
    if (!saveLessonProgress(progressPath, progress, saveError)) TraceLog(LOG_WARNING, "Progress: %s", saveError.c_str());
}

void LessonPlayer::goTo(int index){
    stopStep();
    releaseLessonMedia(media); // the old step's picture, clip or video
    current = std::clamp(index, 0, (int)lesson.steps.size() - 1);
    progress.reached = std::max(progress.reached, current);
    save();
}

void LessonPlayer::startStep(){
    releaseLessonMedia(media); // what runs may need the audio the step's media holds
    if (step().type == LessonStepType::Exercise) running = create(stepExercises[current]);
    else if (step().type == LessonStepType::Play) running = std::make_unique<PlayExercise>((std::filesystem::path(folder) / step().file).string(), playOptions);
}

void LessonPlayer::stopStep(){
    running.reset(); // its destructor puts back what it changed (microphone, keyboard navigation)
}

void LessonPlayer::update(){
    if (!running) return;
    running->update();
    if (!stepPassed(progress, current) && running->lessonScore() >= goal()){
        passStep(progress, current);
        save();
    }
    if (running->wantsToLeave()) stopStep();
}

// While an exercise or song runs: one line at the top, centered (the corners are taken: a drill's Back button,
// a song's score), with the goal and the way back
void LessonPlayer::drawGoalBar(){
    bool passed = stepPassed(progress, current);
    std::string text = passed ? "Goal reached!    " : lessonGoalText(step(), &stepExercises[current]) + TextFormat("  (now %d)    ", running->lessonScore());
    const char* label = passed ? "Continue the lesson" : "Back to the lesson";
    ImGuiStyle& style = ImGui::GetStyle();
    float width = ImGui::CalcTextSize(text.c_str()).x + style.ItemSpacing.x + ImGui::CalcTextSize(label).x + 2 * style.FramePadding.x;
    ImGui::SetCursorPos(ImVec2((ImGui::GetWindowWidth() - width) / 2, BAR_MARGIN));
    ImGui::AlignTextToFramePadding();
    ImGui::PushStyleColor(ImGuiCol_Text, passed ? uiColor(UiColor::Good) : uiColor(UiColor::Dim));
    ImGui::TextUnformatted(text.c_str());
    ImGui::PopStyleColor();
    ImGui::SameLine();
    if (passed){
        ImGui::PushStyleColor(ImGuiCol_Button, uiColorVec(UiColor::Good));
        ImGui::PushStyleColor(ImGuiCol_Text, uiColorVec(UiColor::Card)); // light text on the filled button
    }
    if (ImGui::Button(label)) stopStep();
    if (passed) ImGui::PopStyleColor(2);
}

// A row of dots under the title: done, this one, still ahead
void LessonPlayer::drawDots(){
    int count = (int)lesson.steps.size();
    float width = (count - 1) * DOT_SPACING;
    ImVec2 origin = ImGui::GetCursorScreenPos();
    float x = origin.x + (ImGui::GetWindowWidth() - width) / 2, y = origin.y + DOT_RADIUS;
    ImDrawList* draw = ImGui::GetWindowDrawList();
    for (int i = 0; i < count; i++){
        ImVec2 center(x + i * DOT_SPACING, y);
        if (i == current) draw->AddCircleFilled(center, DOT_RADIUS + 1, uiColor(UiColor::Accent));
        else if (i <= progress.reached) draw->AddCircleFilled(center, DOT_RADIUS, uiColor(UiColor::Dim));
        else draw->AddCircle(center, DOT_RADIUS, uiColor(UiColor::Dim, 0.4f), 0, 1.5f);
    }
    ImGui::Dummy(ImVec2(0, DOT_RADIUS * 2 + 12));
}

void LessonPlayer::draw(){
    if (running){
        running->draw();
        drawGoalBar();
        return;
    }
    float textTop = ImGui::GetCursorPosY();
    ImGui::SetCursorPos(ImVec2(20, 20));
    if (ImGui::Button("Back")) leave = true;
    ImGui::SetCursorPosY(textTop);
    menuTitle(lesson.title.c_str());
    drawDots();

    // The step, centered, above the buttons
    float width = std::min(MAX_STEP_WIDTH, ImGui::GetWindowWidth() - 80);
    float buttonsHeight = 110.0f;
    float height = ImGui::GetWindowHeight() - ImGui::GetCursorPosY() - buttonsHeight;
    ImGui::SetCursorPosX((ImGui::GetWindowWidth() - width) / 2);
    ImGui::BeginChild("Step", ImVec2(width, height));
    drawLessonStep(step(), folder, step().type == LessonStepType::Exercise ? &stepExercises[current] : nullptr, media, width - 20);

    // A step with a goal: start it, and say whether it's passed
    if (hasGoal()){
        ImGui::Dummy(ImVec2(0, 10));
        bool passed = stepPassed(progress, current);
        const char* label = passed ? (step().type == LessonStepType::Play ? "Play again" : "Practice again")
                                   : (step().type == LessonStepType::Play ? "Play" : "Start");
        if (ImGui::Button(label, ImVec2(200, 44))) startStep();
        if (passed) ImGui::TextColored(ImColor(uiColor(UiColor::Good)), "Passed");
        else ImGui::TextColored(ImColor(uiColor(UiColor::Dim)), "Reach the goal to go on");
    }
    ImGui::EndChild();

    // Back and Next (Finish on the last step); Next waits for the step's goal
    bool last = current + 1 == (int)lesson.steps.size();
    float buttonWidth = 200.0f;
    ImGui::SetCursorPosX((ImGui::GetWindowWidth() - 2 * buttonWidth - ImGui::GetStyle().ItemSpacing.x) / 2);
    ImGui::BeginDisabled(current == 0);
    if (ImGui::Button("Previous", ImVec2(buttonWidth, 44))) goTo(current - 1);
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(!canGoOn());
    if (ImGui::Button(last ? "Finish" : "Next", ImVec2(buttonWidth, 44))){
        if (last){
            progress.completed = true;
            save();
            leave = true;
        } else {
            goTo(current + 1);
        }
    }
    ImGui::EndDisabled();
    if (!saveError.empty()) centeredErrorText("Progress could not be saved: " + saveError);
}
