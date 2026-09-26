#include "screens/learnscreen.h"

#include "core/exercisefile.h"
#include "learn/intervalexercise.h"
#include "raylib.h"
#include "ui/ui.h"

#include <algorithm>
#include <filesystem>
#include <memory>

static struct {
    LearnSetup setup;
    std::vector<ExerciseEntry> exercises;  // grouped by category, built-in first in each
    std::vector<std::string> progressText; // one per exercise, e.g. "3/12": read when the menu appears, not every frame
    // The running exercise, whatever kind it is. unique_ptr owns it: resetting it deletes the exercise
    // (running its destructor), so there's no manual delete to forget.
    std::unique_ptr<Exercise> exercise;
} learn;

static std::string progressPath(const ExerciseEntry& entry){
    return (std::filesystem::path(learn.setup.progress) / (entry.id + ".txt")).string();
}

// The only place that knows every exercise type: a new type is a new case here (and its class)
static std::unique_ptr<Exercise> createExercise(const ExerciseEntry& entry){
    switch (entry.exercise.type){
        case ExerciseType::Intervals:
            return std::make_unique<IntervalExercise>(entry.exercise.title, entry.exercise.intervals, progressPath(entry),
                                                      learn.setup.settings.inputDevice);
    }
    return nullptr;
}

static std::string progressSummary(const ExerciseEntry& entry){
    if (!entry.error.empty()) return "";
    switch (entry.exercise.type){
        case ExerciseType::Intervals: {
            const IntervalConfig& config = entry.exercise.intervals;
            size_t unlocked = unlockedIntervals(config, loadIntervalProgress(progressPath(entry))).size();
            return TextFormat("%d/%d", (int)unlocked, (int)config.pool.size());
        }
    }
    return "";
}

static void refreshExercises(){
    learn.exercises = scanExercises(learn.setup.builtInExercises, true);
    std::vector<ExerciseEntry> userExercises = scanExercises(learn.setup.userExercises, false);
    learn.exercises.insert(learn.exercises.end(), userExercises.begin(), userExercises.end());
    // One heading per category: group everything by category. stable_sort keeps the existing order inside
    // each category, so built-in exercises stay first, each group sorted by title.
    std::stable_sort(learn.exercises.begin(), learn.exercises.end(), [](const ExerciseEntry& a, const ExerciseEntry& b){
        return a.exercise.category < b.exercise.category;
    });
    learn.progressText.clear();
    for (const ExerciseEntry& entry : learn.exercises) learn.progressText.push_back(progressSummary(entry));
}

static void endExercise(){
    learn.exercise.reset();
    refreshExercises(); // new progress to show, and files may have been edited meanwhile
}

void openLearnScreen(const LearnSetup& setup){
    learn.setup = setup;
    learn.exercise.reset();
    refreshExercises();
}

void closeLearnScreen(){
    learn.exercise.reset();
}

bool learnBack(){
    if (learn.exercise){
        endExercise();
        return false;
    }
    return true;
}

static void exerciseMenu(bool& leave){
    menuTitle("Learn");
    if (learn.exercises.empty()) centeredText("No exercises found");

    std::string category;
    for (int i = 0; i < (int)learn.exercises.size(); i++){
        const ExerciseEntry& entry = learn.exercises[i];
        if (i == 0 || entry.exercise.category != category){ // a heading whenever the category changes (the list is sorted)
            category = entry.exercise.category;
            ImGui::Dummy(ImVec2(0, 4));
            centeredText(category.c_str());
        }
        std::string label = entry.exercise.title;
        if (!learn.progressText[i].empty()) label += "   " + learn.progressText[i];
        if (!entry.builtIn) label += "  (yours)";

        ImGui::PushID(i);
        if (i == 0) focusNextWhenMenuAppears();
        if (!entry.error.empty()){
            ImGui::BeginDisabled();
            menuButton(label.c_str());
            ImGui::EndDisabled();
            centeredErrorText(entry.error);
        } else {
            if (menuButton(label.c_str())) learn.exercise = createExercise(entry);
            // The description and author, shown when hovering
            std::string details = entry.exercise.description;
            if (!entry.exercise.author.empty()) details += (details.empty() ? "" : "\n") + std::string("by ") + entry.exercise.author;
            if (!details.empty()) ImGui::SetItemTooltip("%s", details.c_str());
        }
        ImGui::PopID();
    }

    ImGui::Dummy(ImVec2(0, 20));
    if (menuButton("Open exercises folder")) openFolder(learn.setup.userExercises);
    if (menuButton("Back")) leave = true;
}

bool learnScreen(){
    beginMenu("Learn");
    bool leave = false;
    if (learn.exercise){
        learn.exercise->update();
        learn.exercise->draw();
        if (learn.exercise->wantsToLeave()) endExercise();
    } else {
        exerciseMenu(leave);
    }
    ImGui::End();
    return leave;
}
