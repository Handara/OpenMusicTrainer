#include "screens/learnscreen.h"

#include "core/exercisefile.h"
#include "core/lesson.h"
#include "core/music.h"
#include "core/plays.h"
#include "core/routine.h"
#include "learn/chordexercise.h"
#include "learn/drillexercise.h"
#include "learn/fretboardexercise.h"
#include "learn/intervalexercise.h"
#include "learn/lessonplayer.h"
#include "learn/neckexercise.h"
#include "learn/routineexercise.h"
#include "learn/singingexercise.h"
#include "raylib.h"
#include "ui/menulist.h"
#include "ui/theme.h"
#include "ui/ui.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <map>
#include <memory>
#include <random>

static struct {
    LearnSetup setup;
    std::vector<ExerciseEntry> exercises;  // grouped by category, built-in first in each
    std::vector<std::string> progressText; // one per exercise, e.g. "3/12": read when the menu appears, not every frame
    std::vector<LessonEntry> lessons;
    std::vector<std::string> lessonProgressText;
    MenuList list;                          // the open kind's list: its selection, kept while exercises run
    MenuList kinds;                         // the first list: the kinds (lessons, each category)
    std::string openKind;                   // the kind whose list is open; "" for the list of kinds
    std::map<std::string, PlayCount> plays; // how many times each was taken up (core/plays)
    // The running exercise, whatever kind it is. unique_ptr owns it: resetting it deletes the exercise
    // (running its destructor), so there's no manual delete to forget.
    std::unique_ptr<Exercise> exercise;
} learn;

static std::string progressPath(const std::string& id){
    return (std::filesystem::path(learn.setup.progress) / (id + ".txt")).string();
}

static std::string playsPath(){
    return (std::filesystem::path(learn.setup.progress) / "plays.txt").string();
}

// One more time for an exercise or a lesson (its id), today
static void countPlay(const std::string& id){
    int year, month, day;
    dateFromDays(today(), year, month, day);
    std::string error;
    if (!recordPlay(playsPath(), id, TextFormat("%04d-%02d-%02d", year, month, day), error)) TraceLog(LOG_WARNING, "Progress: %s", error.c_str());
}

// "done 12 times, last on 2026-10-05", or "" for never
static std::string playsText(const std::string& id){
    auto found = learn.plays.find(id);
    if (found == learn.plays.end() || found->second.times == 0) return "";
    const PlayCount& count = found->second;
    return TextFormat("done %d %s, last %s", count.times, count.times == 1 ? "time" : "times", count.last.c_str());
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
        case ExerciseType::Scale: {
            const ScaleDrillConfig& config = entry.exercise.drill;
            std::vector<DrillNote> notes;
            std::string error;
            buildScaleDrill(config, notes, error); // the file was checked when it loaded, so this succeeds
            const ScaleInfo* scale = findScale(config.scale);
            DrillSetup setup;
            setup.about = std::string(scale ? scale->displayName : "") + " in " + pitchClassName(config.rootPitchClass);
            setup.tempo = config.tempo;
            setup.tuning = config.tuning;
            setup.key = scaleDrillKey(config);
            setup.nextPass = [notes](){ return notes; }; // the same scale every pass
            return std::make_unique<DrillExercise>(entry.exercise.title, setup, progressPath(entry), learn.setup.settings);
        }
        case ExerciseType::Neck:
            return std::make_unique<NeckExercise>(entry.exercise.title, entry.exercise.neck, entry.exercise.neckOnBass, progressPath(entry),
                                                  learn.setup.settings);
        case ExerciseType::Reading: {
            const ReadingConfig& config = entry.exercise.reading;
            const ScaleInfo* scale = findScale(config.scale);
            DrillSetup setup;
            std::string scaleName = scale ? scale->displayName : "";
            if (!scaleName.empty()) scaleName[0] = (char)std::tolower((unsigned char)scaleName[0]); // "G major", as it's said
            setup.about = TextFormat("Reading in %s %s, frets %d to %d", pitchClassName(config.rootPitchClass),
                                     scaleName.c_str(), config.lowestFret, config.highestFret);
            setup.tempo = config.tempo;
            setup.tuning = config.tuning;
            setup.key = scale ? scaleKeySignature(config.rootPitchClass, *scale) : KeySignature{};
            setup.beatsPerBar = config.beatsPerBar;
            setup.staffOnly = true;
            // A new melody every pass (the file was checked when it loaded, so there's always one)
            setup.nextPass = [config, rng = std::mt19937(std::random_device{}())]() mutable {
                std::vector<DrillNote> notes;
                std::string error;
                buildReading(config, rng, notes, error);
                return notes;
            };
            return std::make_unique<DrillExercise>(entry.exercise.title, setup, progressPath(entry), learn.setup.settings);
        }
        case ExerciseType::Rhythm: {
            const RhythmConfig& config = entry.exercise.rhythm;
            DrillSetup setup;
            for (const std::string& cell : config.cells) setup.about += (setup.about.empty() ? "" : ", ") + cell;
            for (char& c : setup.about) if (c == '_') c = ' ';
            setup.about = TextFormat("Rhythm in %d/4: %s", config.beatsPerBar, setup.about.c_str());
            setup.tempo = config.tempo;
            setup.tuning = config.tuning;
            setup.beatsPerBar = config.beatsPerBar;
            setup.timingOnly = true;
            // A new rhythm every pass: the generator keeps its own random numbers
            setup.nextPass = [config, rng = std::mt19937(std::random_device{}())]() mutable { return buildRhythm(config, rng); };
            return std::make_unique<DrillExercise>(entry.exercise.title, setup, progressPath(entry), learn.setup.settings);
        }
        case ExerciseType::Singing:
            return std::make_unique<SingingExercise>(entry.exercise.title, entry.exercise.singing, progressPath(entry),
                                                     learn.setup.settings.inputDevice, learn.setup.settings.voiceChannel);
        case ExerciseType::Chords:
            return std::make_unique<ChordExercise>(entry.exercise.title, entry.exercise.chords, progressPath(entry), learn.setup.settings);
        case ExerciseType::Fretboard:
            return std::make_unique<FretboardExercise>(entry.exercise.title, entry.exercise.fretboard, progressPath(entry),
                                                       learn.setup.settings.inputDevice,
                                                       channelFor(learn.setup.settings, roleForTuning(
                                                           *std::min_element(entry.exercise.fretboard.tuning.begin(), entry.exercise.fretboard.tuning.end()))));
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
        case ExerciseType::Scale:
        case ExerciseType::Rhythm:
        case ExerciseType::Reading:
        case ExerciseType::Chords: {
            int best = loadDrillProgress(progressPath(entry)).bestCleanTempo;
            return best > 0 ? TextFormat("best %d bpm", best) : "";
        }
        case ExerciseType::Fretboard:
        case ExerciseType::Singing: {
            int best = loadQuizProgress(progressPath(entry)).bestStreak;
            return best > 0 ? TextFormat("best streak %d", best) : "";
        }
        case ExerciseType::Neck: {
            const NeckStats stats = loadNeckStats(progressPath(entry));
            return stats.runs.empty() ? "" : TextFormat("%d %s", (int)stats.runs.size(), stats.runs.size() == 1 ? "run" : "runs");
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
    learn.plays = loadPlays(playsPath());

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
    if (!learn.openKind.empty()){ // inside a kind: back to the kinds
        learn.openKind.clear();
        return false;
    }
    return true;
}

// What a row of the Learn menu stands for
struct LearnRow {
    enum Kind { Heading, Lesson, Exercise, OpenExercises, OpenLessons } kind;
    int index; // into learn.lessons or learn.exercises
};

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
        const std::string plays = playsText(entry.id);
        if (!plays.empty()) progress += (progress.empty() ? "" : "  ·  ") + plays;
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

static bool wantsEditor = false;

bool learnWantsEditor(){
    const bool wants = wantsEditor;
    wantsEditor = false;
    return wants;
}

// The Learn menu, in two: first the kinds (lessons, then each category of exercises), each with how much there is and
// how often it's been taken up; Enter opens one, its exercises with their progress and how many times each was done
static void exerciseMenu(){
    float s = menuScale();
    const bool inKind = !learn.openKind.empty();
    menuScreenTitle(inKind ? learn.openKind.c_str() : "Learn", s);
    if (!inKind){
        // Beside the title: learning, or making lessons (Tab)
        const char* const modes[] = { "LEARN", "EDIT LESSONS" };
        int mode = 0;
        menuSwitchRow("MODE", modes, 2, mode, ImGui::GetWindowWidth() * 0.55f, ImGui::GetWindowHeight() * 0.09f + 14 * s, s);
        wantsEditor = mode == 1 || ImGui::IsKeyPressed(ImGuiKey_Tab);
    }
    const std::string LESSONS = "Lessons";
    std::vector<MenuRow> rows;
    std::vector<LearnRow> targets;
    std::vector<std::string> kinds; // inKind == false: each row's kind ("" for the folders)
    float width = ImGui::GetWindowWidth(), height = ImGui::GetWindowHeight();
    if (!inKind){
        // The kinds, lessons first (where a beginner starts), each with how many and how often they've been done
        auto addKind = [&](const std::string& kind, int count, int times, const char* noun){
            MenuRow row;
            row.label = kind;
            row.detail = TextFormat("%d %s%s", count, noun, count == 1 ? "" : "s");
            if (times > 0) row.detail += TextFormat("  ·  done %d %s", times, times == 1 ? "time" : "times");
            rows.push_back(row);
            targets.push_back({ LearnRow::Heading, -1 });
            kinds.push_back(kind);
        };
        if (!learn.lessons.empty()){
            int times = 0;
            for (const LessonEntry& entry : learn.lessons) times += learn.plays.count(entry.id) ? learn.plays[entry.id].times : 0;
            addKind(LESSONS, (int)learn.lessons.size(), times, "lesson");
        }
        for (size_t i = 0; i < learn.exercises.size();){
            const std::string& category = learn.exercises[i].exercise.category;
            int count = 0, times = 0;
            for (; i < learn.exercises.size() && learn.exercises[i].exercise.category == category; i++){
                count++;
                times += learn.plays.count(learn.exercises[i].id) ? learn.plays[learn.exercises[i].id].times : 0;
            }
            addKind(category, count, times, "exercise");
        }
        MenuRow folders;
        folders.label = "YOUR OWN";
        folders.heading = true;
        rows.push_back(folders);
        targets.push_back({ LearnRow::Heading, -1 });
        kinds.push_back("");
        rows.push_back(actionRow("Open exercises folder"));
        targets.push_back({ LearnRow::OpenExercises, -1 });
        kinds.push_back("");
        rows.push_back(actionRow("Open lessons folder"));
        targets.push_back({ LearnRow::OpenLessons, -1 });
        kinds.push_back("");
        int confirmed = menuList(learn.kinds, rows, { ImVec2(width * 0.07f, height * 0.2f), width * 0.48f, height * 0.72f - 20 * s, s });
        menuScreenHint("Up/Down  choose    Enter  open    Tab  edit lessons    Esc  back", s);
        if (confirmed < 0) return;
        if (!kinds[confirmed].empty()){
            learn.openKind = kinds[confirmed];
            learn.list.selected = -1; // its first
            return;
        }
        if (targets[confirmed].kind == LearnRow::OpenExercises) openFolder(learn.setup.userExercises);
        if (targets[confirmed].kind == LearnRow::OpenLessons) openFolder(learn.setup.userLessons);
        return;
    }

    // Inside a kind: its lessons or exercises, with their progress and how many times each was done
    auto detail = [](std::string progress, const std::string& id, bool builtIn){
        const std::string plays = playsText(id);
        if (!plays.empty()) progress += (progress.empty() ? "" : "  ·  ") + plays;
        if (!builtIn) progress += progress.empty() ? "yours" : "  ·  yours";
        return progress;
    };
    if (learn.openKind == LESSONS){
        for (int i = 0; i < (int)learn.lessons.size(); i++){
            const LessonEntry& entry = learn.lessons[i];
            MenuRow row;
            row.label = entry.lesson.title;
            row.detail = detail(learn.lessonProgressText[i], entry.id, entry.builtIn);
            row.note = entry.error;
            row.disabled = !entry.error.empty();
            rows.push_back(row);
            targets.push_back({ LearnRow::Lesson, i });
        }
    }
    for (int i = 0; i < (int)learn.exercises.size(); i++){
        const ExerciseEntry& entry = learn.exercises[i];
        if (entry.exercise.category != learn.openKind) continue;
        MenuRow row;
        row.label = entry.exercise.title;
        row.detail = detail(learn.progressText[i], entry.id, entry.builtIn);
        row.note = entry.error;
        row.disabled = !entry.error.empty();
        rows.push_back(row);
        targets.push_back({ LearnRow::Exercise, i });
    }
    if (rows.empty()){ // the kind is gone (its files were removed meanwhile)
        learn.openKind.clear();
        return;
    }
    int confirmed = menuList(learn.list, rows, { ImVec2(width * 0.07f, height * 0.2f), width * 0.48f, height * 0.72f - 20 * s, s });
    if (learn.list.selected >= 0 && learn.list.selected < (int)targets.size()) drawAbout(targets[learn.list.selected], s);
    menuScreenHint("Up/Down  choose    Enter  start    Esc  back to the kinds", s);
    if (confirmed < 0) return;
    const LearnRow& target = targets[confirmed];
    if (target.kind == LearnRow::Lesson){
        countPlay(learn.lessons[target.index].id);
        learn.exercise = openLesson(learn.lessons[target.index]);
    } else if (target.kind == LearnRow::Exercise){
        countPlay(learn.exercises[target.index].id);
        learn.exercise = createExercise(learn.exercises[target.index]);
    }
}

void learnScreen(){
    beginMenu("Learn");
    if (learn.exercise){
        learn.exercise->update();
        learn.exercise->draw();
        if (learn.exercise->wantsToLeave()) endExercise();
    } else {
        exerciseMenu();
    }
    ImGui::End();
}
