#include "screens/learnscreen.h"

#include "core/intervals.h"
#include "learn/intervalexercise.h"
#include "raylib.h"
#include "ui/ui.h"

#include <filesystem>
#include <memory>

struct IntervalMode {
    IntervalDirection direction;
    const char* label;
    const char* progressFile;
};
const int INTERVAL_MODE_COUNT = 3;
const IntervalMode INTERVAL_MODES[INTERVAL_MODE_COUNT] = {
    { IntervalDirection::Ascending, "Intervals going up", "intervals_up.txt" },
    { IntervalDirection::Descending, "Intervals going down", "intervals_down.txt" },
    { IntervalDirection::Harmonic, "Intervals played together", "intervals_together.txt" },
};

static struct {
    std::string progressDir;
    // The running exercise, whatever kind it is. unique_ptr owns it: resetting it deletes the exercise
    // (running its destructor), so there's no manual delete to forget.
    std::unique_ptr<Exercise> exercise;
    int unlocked[INTERVAL_MODE_COUNT] = {}; // shown on the menu buttons: read from disk when the menu appears, not every frame
} learn;

static std::string progressPath(const char* file){
    return (std::filesystem::path(learn.progressDir) / file).string();
}

static void refreshProgressSummaries(){
    for (int i = 0; i < INTERVAL_MODE_COUNT; i++){
        IntervalConfig config;
        learn.unlocked[i] = (int)unlockedIntervals(config, loadIntervalProgress(progressPath(INTERVAL_MODES[i].progressFile))).size();
    }
}

static void endExercise(){
    learn.exercise.reset();
    refreshProgressSummaries(); // the exercise saved new progress
}

void openLearnScreen(const std::string& progressDir){
    learn.progressDir = progressDir;
    learn.exercise.reset();
    refreshProgressSummaries();
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

bool learnScreen(){
    beginMenu("Learn");
    bool leave = false;
    if (learn.exercise){
        learn.exercise->update();
        learn.exercise->draw();
        if (learn.exercise->wantsToLeave()) endExercise();
    } else {
        menuTitle("Learn");
        centeredText("Ear training");
        ImGui::Dummy(ImVec2(0, 6));
        for (int i = 0; i < INTERVAL_MODE_COUNT; i++){
            // Each mode keeps its own progress; the button shows how far along it is
            const IntervalMode& mode = INTERVAL_MODES[i];
            std::string label = std::string(mode.label) + TextFormat("   %d/%d", learn.unlocked[i], INTERVAL_COUNT);
            if (i == 0) focusNextWhenMenuAppears();
            if (menuButton(label.c_str())){
                IntervalConfig config;
                config.direction = mode.direction;
                learn.exercise = std::make_unique<IntervalExercise>(mode.label, config, progressPath(mode.progressFile));
            }
        }
        ImGui::Dummy(ImVec2(0, 20));
        if (menuButton("Back")) leave = true;
    }
    ImGui::End();
    return leave;
}
