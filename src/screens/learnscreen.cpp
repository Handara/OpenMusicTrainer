#include "screens/learnscreen.h"

#include "core/exercisefile.h"
#include "learn/drillexercise.h"
#include "core/lesson.h"
#include "learn/intervalexercise.h"
#include "learn/lessonplayer.h"
#include "learn/routineexercise.h"
#include "raylib.h"
#include "ui/ui.h"

#include <algorithm>
#include <filesystem>
#include <memory>

static struct {
    LearnSetup setup;
    std::vector<ExerciseEntry> exercises;  // grouped by category, built-in first in each
    std::vector<std::string> progressText; // one per exercise, e.g. "3/12": read when the menu appears, not every frame
    std::vector<LessonEntry> lessons;
    std::vector<std::string> lessonProgressText;
    // The running exercise, whatever kind it is. unique_ptr owns it: resetting it deletes the exercise
    // (running its destructor), so there's no manual delete to forget.
    std::unique_ptr<Exercise> exercise;
} learn;

static std::string progressPath(const std::string& id){
    return (std::filesystem::path(learn.setup.progress) / (id + ".txt")).string();
}

static std::string progressPath(const ExerciseEntry& entry){
    return progressPath(entry.id);
}

// The only place that knows every exercise type: a new type is a new case here (and its class)
static std::unique_ptr<Exercise> createExercise(const ExerciseEntry& entry){
    switch (entry.exercise.type){
        case ExerciseType::Intervals:
            return std::make_unique<IntervalExercise>(entry.exercise.title, entry.exercise.intervals, progressPath(entry),
                                                      learn.setup.settings.inputDevice);
        case ExerciseType::Scale:
            return std::make_unique<DrillExercise>(entry.exercise.title, entry.exercise.drill, progressPath(entry), learn.setup.settings);
        case ExerciseType::Routine: {
            std::vector<RoutineExercise::Step> steps;
            for (const RoutineStep& step : entry.exercise.routine){
                // Always found: checkRoutines gave the routine an error otherwise, and it couldn't be started
                steps.push_back({*findExercise(learn.exercises, entry.builtIn, step.exercise), step.minutes * 60.0});
            }
            // It's handed this very function to start its steps with
            return std::make_unique<RoutineExercise>(entry.exercise.title, steps, progressPath(entry), createExercise);
        }
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
        case ExerciseType::Scale: {
            int best = loadDrillProgress(progressPath(entry)).bestCleanTempo;
            return best > 0 ? TextFormat("best %d bpm", best) : "";
        }
        case ExerciseType::Routine: {
            RoutineProgress progress = loadRoutineProgress(progressPath(entry));
            int day = today(), streak = currentStreak(progress, day);
            std::string summary = doneOnDay(progress, day) ? "done today" : "";
            if (streak > 1) summary += TextFormat("%s%d day streak", summary.empty() ? "" : ", ", streak);
            return summary;
        }
    }
    return "";
}

static void refreshExercises(){
    learn.exercises = scanExercises(learn.setup.builtInExercises, true);
    std::vector<ExerciseEntry> userExercises = scanExercises(learn.setup.userExercises, false);
    learn.exercises.insert(learn.exercises.end(), userExercises.begin(), userExercises.end());
    checkRoutines(learn.exercises); // needs every exercise, built-in and the player's
    // One heading per category: group everything by category. stable_sort keeps the existing order inside
    // each category, so built-in exercises stay first, each group sorted by title.
    std::stable_sort(learn.exercises.begin(), learn.exercises.end(), [](const ExerciseEntry& a, const ExerciseEntry& b){
        return a.exercise.category < b.exercise.category;
    });
    learn.progressText.clear();
    for (const ExerciseEntry& entry : learn.exercises) learn.progressText.push_back(progressSummary(entry));

    // Lessons, built-in first; their exercise steps are checked against the exercises just loaded
    learn.lessons = scanLessons(learn.setup.builtInLessons, true);
    std::vector<LessonEntry> userLessons = scanLessons(learn.setup.userLessons, false);
    learn.lessons.insert(learn.lessons.end(), userLessons.begin(), userLessons.end());
    checkLessonExercises(learn.lessons, learn.exercises);
    learn.lessonProgressText.clear();
    for (const LessonEntry& entry : learn.lessons){
        LessonProgress progress = loadLessonProgress(progressPath(entry.id));
        int steps = (int)entry.lesson.steps.size();
        if (!entry.error.empty()) learn.lessonProgressText.push_back("");
        else if (progress.completed) learn.lessonProgressText.push_back("done");
        else if (progress.reached > 0) learn.lessonProgressText.push_back(TextFormat("step %d of %d", progress.reached + 1, steps));
        else learn.lessonProgressText.push_back("");
    }
}

static std::unique_ptr<Exercise> openLesson(const LessonEntry& entry){
    // Each step's exercise, found now: a copy, since the lists are rebuilt when the lesson ends
    std::vector<ExerciseEntry> stepExercises;
    for (const LessonStep& step : entry.lesson.steps){
        const ExerciseEntry* found = step.type == LessonStepType::Exercise ? findExercise(learn.exercises, entry.builtIn, step.exercise) : nullptr;
        stepExercises.push_back(found ? *found : ExerciseEntry{});
    }
    // Play steps play like songs, with the player's own settings
    const Settings& settings = learn.setup.settings;
    GameplayOptions play;
    play.noteSpeed = settings.noteSpeed;
    play.offsetSeconds = settings.globalOffsetMs / 1000.0f;
    play.lowStringOnTop = settings.lowStringOnTop;
    play.noteViews = settings.noteViews;
    play.playWithInstrument = settings.playWithInstrument;
    play.inputDevice = settings.inputDevice;
    play.inputOffsetSeconds = settings.inputOffsetMs / 1000.0f;
    return std::make_unique<LessonPlayer>(entry, stepExercises, createExercise, play, progressPath(entry.id));
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
    bool focusGiven = false;

    // Lessons first: they're where a beginner starts
    if (!learn.lessons.empty()){
        centeredText("Lessons");
        for (int i = 0; i < (int)learn.lessons.size(); i++){
            const LessonEntry& entry = learn.lessons[i];
            std::string label = entry.lesson.title;
            if (!learn.lessonProgressText[i].empty()) label += "   " + learn.lessonProgressText[i];
            if (!entry.builtIn) label += "  (yours)";
            ImGui::PushID(("lesson" + std::to_string(i)).c_str());
            if (!focusGiven){ focusNextWhenMenuAppears(); focusGiven = true; }
            ImGui::BeginDisabled(!entry.error.empty());
            if (menuButton(label.c_str())) learn.exercise = openLesson(entry);
            ImGui::EndDisabled();
            if (!entry.error.empty()) centeredErrorText(entry.error);
            else if (!entry.lesson.description.empty()) ImGui::SetItemTooltip("%s", entry.lesson.description.c_str());
            ImGui::PopID();
        }
        ImGui::Dummy(ImVec2(0, 10));
    }
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
        if (!focusGiven){ focusNextWhenMenuAppears(); focusGiven = true; }
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
    if (menuButton("Open lessons folder")) openFolder(learn.setup.userLessons);
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
