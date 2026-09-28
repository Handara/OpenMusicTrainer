#include "screens/learnscreen.h"

#include "core/exercisefile.h"
#include "core/lesson.h"
#include "learn/drillexercise.h"
#include "learn/intervalexercise.h"
#include "learn/lessonplayer.h"
#include "learn/routineexercise.h"
#include "raylib.h"
#include "ui/menulist.h"
#include "ui/theme.h"
#include "ui/ui.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <memory>

static struct {
    LearnSetup setup;
    std::vector<ExerciseEntry> exercises;  // grouped by category, built-in first in each
    std::vector<std::string> progressText; // one per exercise, e.g. "3/12": read when the menu appears, not every frame
    std::vector<LessonEntry> lessons;
    std::vector<std::string> lessonProgressText;
    MenuList list;                          // the menu's selection, kept while exercises run
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

// What a row of the Learn menu stands for
struct LearnRow {
    enum Kind { Heading, Lesson, Exercise, OpenExercises, OpenLessons, Back } kind;
    int index; // into learn.lessons or learn.exercises
};

static std::string upper(std::string text){
    for (char& c : text) c = (char)std::toupper((unsigned char)c);
    return text;
}

// The selected lesson or exercise, on a card on the right: its title, what it's about, who made it, how far you are
static void drawAbout(const LearnRow& row, float s){
    std::string title, about, author, progress;
    if (row.kind == LearnRow::Lesson){
        const LessonEntry& entry = learn.lessons[row.index];
        title = entry.lesson.title;
        about = entry.lesson.description;
        author = entry.lesson.author;
        progress = TextFormat("%d %s", (int)entry.lesson.steps.size(), entry.lesson.steps.size() == 1 ? "step" : "steps");
        if (!learn.lessonProgressText[row.index].empty()) progress += "  ·  " + learn.lessonProgressText[row.index];
    } else if (row.kind == LearnRow::Exercise){
        const ExerciseEntry& entry = learn.exercises[row.index];
        title = entry.exercise.title;
        about = entry.exercise.description;
        author = entry.exercise.author;
        progress = learn.progressText[row.index];
    } else {
        return;
    }
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const UiFonts& fonts = uiFonts();
    float width = ImGui::GetWindowWidth(), height = ImGui::GetWindowHeight();
    ImVec2 card(width * 0.58f, height * 0.25f);
    float cardWidth = width * 0.35f, pad = 26 * s, inner = cardWidth - 2 * pad;
    auto measure = [&](ImFont* font, float size, const std::string& text){
        return text.empty() || !font ? 0.0f : font->CalcTextSizeA(size, FLT_MAX, inner, text.c_str()).y;
    };
    float titleHeight = measure(fonts.bold, 24 * s, title), aboutHeight = measure(fonts.text, 18 * s, about);
    float cardHeight = pad * 2 + titleHeight + 12 * s + (about.empty() ? 0 : aboutHeight + 14 * s)
                     + (author.empty() ? 0 : 16 * s + 10 * s) + (progress.empty() ? 0 : 18 * s);
    draw->AddRectFilled(ImVec2(card.x, card.y + 3 * s), ImVec2(card.x + cardWidth, card.y + cardHeight + 3 * s), uiColor(UiColor::Ink, 0.04f), 10 * s);
    draw->AddRectFilled(card, ImVec2(card.x + cardWidth, card.y + cardHeight), uiColor(UiColor::Card), 10 * s);
    float x = card.x + pad, y = card.y + pad;
    draw->AddText(fonts.bold, 24 * s, ImVec2(x, y), uiColor(UiColor::Ink), title.c_str(), nullptr, inner);
    y += titleHeight + 12 * s;
    if (!about.empty()){
        draw->AddText(fonts.text, 18 * s, ImVec2(x, y), uiColor(UiColor::Ink), about.c_str(), nullptr, inner);
        y += aboutHeight + 14 * s;
    }
    if (!author.empty()){
        draw->AddText(fonts.text, 16 * s, ImVec2(x, y), uiColor(UiColor::Dim), ("by " + author).c_str());
        y += 16 * s + 10 * s;
    }
    if (!progress.empty()) draw->AddText(fonts.bold, 18 * s, ImVec2(x, y), uiColor(UiColor::Accent), progress.c_str());
}

static void exerciseMenu(bool& leave){
    float s = menuScale();
    menuScreenTitle("Learn", s);

    // The rows: lessons first (where a beginner starts), then each category of exercises, then the folders and Back
    std::vector<MenuRow> rows;
    std::vector<LearnRow> targets;
    auto heading = [&](const std::string& text){
        MenuRow row;
        row.label = upper(text);
        row.heading = true;
        rows.push_back(row);
        targets.push_back({LearnRow::Heading, -1});
    };
    if (!learn.lessons.empty()) heading("Lessons");
    for (int i = 0; i < (int)learn.lessons.size(); i++){
        const LessonEntry& entry = learn.lessons[i];
        MenuRow row;
        row.label = entry.lesson.title;
        row.detail = learn.lessonProgressText[i];
        if (!entry.builtIn) row.detail += row.detail.empty() ? "yours" : "  ·  yours";
        row.note = entry.error;
        row.disabled = !entry.error.empty();
        rows.push_back(row);
        targets.push_back({LearnRow::Lesson, i});
    }
    std::string category;
    for (int i = 0; i < (int)learn.exercises.size(); i++){
        const ExerciseEntry& entry = learn.exercises[i];
        if (i == 0 || entry.exercise.category != category){ // a heading whenever the category changes (the list is sorted)
            category = entry.exercise.category;
            heading(category);
        }
        MenuRow row;
        row.label = entry.exercise.title;
        row.detail = learn.progressText[i];
        if (!entry.builtIn) row.detail += row.detail.empty() ? "yours" : "  ·  yours";
        row.note = entry.error;
        row.disabled = !entry.error.empty();
        rows.push_back(row);
        targets.push_back({LearnRow::Exercise, i});
    }
    heading("Your own");
    rows.push_back(actionRow("Open exercises folder"));
    targets.push_back({LearnRow::OpenExercises, -1});
    rows.push_back(actionRow("Open lessons folder"));
    targets.push_back({LearnRow::OpenLessons, -1});
    rows.push_back(actionRow("Back", "Esc"));
    targets.push_back({LearnRow::Back, -1});

    float width = ImGui::GetWindowWidth(), height = ImGui::GetWindowHeight();
    int confirmed = menuList(learn.list, rows, {ImVec2(width * 0.07f, height * 0.2f), width * 0.48f, height * 0.72f - 20 * s, s});
    if (learn.list.selected >= 0) drawAbout(targets[learn.list.selected], s);
    menuScreenHint("Up/Down  choose    Enter  start    Esc  back", s);
    if (confirmed < 0) return;
    const LearnRow& target = targets[confirmed];
    switch (target.kind){
        case LearnRow::Lesson:        learn.exercise = openLesson(learn.lessons[target.index]); break;
        case LearnRow::Exercise:      learn.exercise = createExercise(learn.exercises[target.index]); break;
        case LearnRow::OpenExercises: openFolder(learn.setup.userExercises); break;
        case LearnRow::OpenLessons:   openFolder(learn.setup.userLessons); break;
        case LearnRow::Back:          leave = true; break;
        case LearnRow::Heading:       break;
    }
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
