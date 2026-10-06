#include "screens/learnscreen.h"

#include "core/course.h"
#include "core/exercisefile.h"
#include "core/lesson.h"
#include "core/music.h"
#include "core/plays.h"
#include "core/routine.h"
#include "learn/chordexercise.h"
#include "learn/coursechapter.h"
#include "learn/drillexercise.h"
#include "learn/fretboardexercise.h"
#include "learn/intervalexercise.h"
#include "learn/lessonplayer.h"
#include "learn/neckexercise.h"
#include "learn/neckwalkexercise.h"
#include "learn/notequizexercise.h"
#include "learn/routineexercise.h"
#include "learn/singingexercise.h"
#include "raylib.h"
#include "ui/menulist.h"
#include "ui/theme.h"
#include "ui/ui.h"

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cmath>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <random>

static struct {
    LearnSetup setup;
    std::vector<ExerciseEntry> exercises;  // grouped by category, built-in first in each
    std::vector<std::string> progressText; // one per exercise, e.g. "3/12": read when the menu appears, not every frame
    std::vector<LessonEntry> lessons;
    std::vector<std::string> lessonProgressText;
    std::vector<CourseEntry> courses;
    std::vector<CourseScores> courseScores; // each course's drills' best scores
    int openCourse = -1;                    // the course open (its levels); -1 for none
    int openLevel = -1;                     //   and its level open (its chapters); -1 for none
    int courseRow = 0, levelRow = 0;        // the row chosen on each (the course's first row is Continue)
    float courseScroll = 0.0f, levelScroll = 0.0f;
    MenuList list;                          // the open category's list: its selection, kept while exercises run
    MenuList sections[3];                   // each section's list (courses and lessons, drills, games)
    int section = 0;                        // the section shown
    std::string openKind;                   // the category of drills whose list is open; "" for none
    std::map<std::string, PlayCount> plays; // how many times each was taken up (core/plays)
    // The running exercise, whatever kind it is. unique_ptr owns it: resetting it deletes the exercise
    // (running its destructor), so there's no manual delete to forget.
    std::unique_ptr<Exercise> exercise;
    InputRole instrument = InputRole::Guitar;         // what's played: only its courses and exercises are listed
    bool instrumentChanged = false;                   //   switched this frame
    std::function<std::unique_ptr<Exercise>()> pending; // waiting for the tuning check
    bool tuningAsked = false;
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

// Listed for the instrument played: one of its own, or one for any
static bool forInstrument(const ExerciseFile& exercise){
    if (exercise.instrument == ExerciseInstrument::Any) return true;
    return (exercise.instrument == ExerciseInstrument::Bass) == (learn.instrument == InputRole::Bass);
}
static bool forInstrument(const Course& course){
    return course.bass == (learn.instrument == InputRole::Bass);
}

// Something to start: played on the instrument, it waits for the tuning check (main asks, learnWantsTuning)
static void startWhenTuned(bool onInstrument, std::function<std::unique_ptr<Exercise>()> make){
    if (!onInstrument){
        learn.exercise = make();
        return;
    }
    learn.pending = std::move(make);
    learn.tuningAsked = false;
}

bool learnWantsTuning(InputRole& instrument){
    if (!learn.pending || learn.tuningAsked) return false;
    learn.tuningAsked = true;
    instrument = learn.instrument;
    return true;
}

void learnTuningDone(bool go){
    if (go && learn.pending) learn.exercise = learn.pending();
    learn.pending = nullptr;
    learn.tuningAsked = false;
}

bool learnChangedInstrument(InputRole& instrument){
    if (!learn.instrumentChanged) return false;
    learn.instrumentChanged = false;
    instrument = learn.instrument;
    return true;
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
        case ExerciseType::Notes:
            return std::make_unique<NoteQuizExercise>(entry.exercise.title, entry.exercise.noteQuiz, entry.exercise.noteQuizKey,
                                                      entry.exercise.neckOnBass, progressPath(entry), learn.setup.settings);
        case ExerciseType::NeckWalk: {
            // Its tune beside the exercise, or else in the game's own games folder
            namespace fs = std::filesystem;
            fs::path tune = fs::path(entry.path).parent_path() / entry.exercise.tune;
            if (!fs::exists(tune)) tune = fs::path(learn.setup.builtInExercises).parent_path() / "games" / entry.exercise.tune;
            return std::make_unique<NeckWalkExercise>(entry.exercise.title, tune.string(), entry.exercise.neckOnBass, entry.exercise.walkLevel,
                                                      progressPath(entry), learn.setup.settings);
        }
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
        case ExerciseType::Notes: {
            const NoteQuizStats stats = loadNoteQuizStats(progressPath(entry));
            return stats.runs == 0 ? "" : TextFormat("passed %d of %d", stats.passed, stats.runs);
        }
        case ExerciseType::NeckWalk: {
            const NeckWalkStats stats = loadNeckWalkStats(progressPath(entry));
            if (stats.games.empty()) return "";
            const int level = stats.chosen ? stats.choice.level : 0;
            return TextFormat("best %lld on %s  ·  %d %s", neckWalkBest(stats, level), neckWalkLevel(level).name, (int)stats.games.size(),
                              stats.games.size() == 1 ? "game" : "games");
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

    // Courses, and which of their lessons are done (a lesson's own progress, kept by the course's id and its title)
    learn.courses = scanCourses((std::filesystem::path(learn.setup.builtInExercises).parent_path() / "courses").string());
    learn.courseScores.clear();
    for (CourseEntry& entry : learn.courses){
        learn.courseScores.push_back(loadCourseScores(progressPath(entry.id)));
        // A named exercise must be there
        for (const CourseLesson& lesson : entry.course.lessons)
            for (const LessonStep& step : lesson.lesson.steps)
                if (entry.error.empty() && step.type == LessonStepType::Exercise && !step.inlined && !findExercise(learn.exercises, true, step.exercise))
                    entry.error = "the chapter '" + lesson.lesson.title + "' uses the exercise '" + step.exercise + "', which isn't there";
    }

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

// Play steps play like songs, with the player's own settings
static GameplayOptions lessonPlayOptions(){
    const Settings& settings = learn.setup.settings;
    GameplayOptions play;
    play.noteSpeed = settings.noteSpeed;
    play.offsetSeconds = settings.globalOffsetMs / 1000.0f;
    play.lowStringOnTop = settings.lowStringOnTop;
    play.noteViews = settings.noteViews;
    play.playWithInstrument = settings.playWithInstrument;
    play.inputDevice = settings.inputDevice;
    play.inputOffsetSeconds = settings.inputOffsetMs / 1000.0f;
    return play;
}

static std::unique_ptr<Exercise> openLesson(const LessonEntry& entry){
    // Each step's exercise, found now: a copy, since the lists are rebuilt when the lesson ends
    std::vector<ExerciseEntry> stepExercises;
    for (const LessonStep& step : entry.lesson.steps){
        const ExerciseEntry* found = step.type == LessonStepType::Exercise ? findExercise(learn.exercises, entry.builtIn, step.exercise) : nullptr;
        stepExercises.push_back(found ? *found : ExerciseEntry{});
    }
    return std::make_unique<LessonPlayer>(entry, stepExercises, createExercise, lessonPlayOptions(), progressPath(entry.id));
}

// A course's chapter: its drills, each the exercise its step runs (written in place: kept by the course and drill)
static std::unique_ptr<Exercise> openCourseChapter(int courseIndex, int lessonIndex){
    const CourseEntry& entry = learn.courses[courseIndex];
    const CourseLesson& chapter = entry.course.lessons[lessonIndex];
    std::vector<ExerciseEntry> drills;
    for (const CourseDrill& drill : courseDrills(entry.course, lessonIndex)){
        const LessonStep& step = chapter.lesson.steps[drill.step];
        ExerciseEntry exercise;
        if (step.inlined){
            exercise.name = drill.id;
            exercise.id = entry.id + "-" + drill.id;
            exercise.builtIn = true;
            exercise.exercise = step.inlineExercise;
        } else if (const ExerciseEntry* found = findExercise(learn.exercises, true, step.exercise)){
            exercise = *found;
        }
        drills.push_back(exercise);
    }
    return std::make_unique<CourseChapter>(entry.course, lessonIndex, drills, createExercise, progressPath(entry.id));
}

static void endExercise(){
    learn.exercise.reset();
    refreshExercises(); // new progress to show, and files may have been edited meanwhile
    // Back from a course's chapter: on to the next one, if it's passed now
    if (learn.openCourse >= 0 && learn.openCourse < (int)learn.courses.size() && learn.openLevel >= 0){
        const Course& course = learn.courses[learn.openCourse].course;
        const CourseUnit& unit = course.units[learn.openLevel];
        const int chapter = unit.firstLesson + learn.levelRow;
        if (learn.levelRow + 1 < unit.lessonCount && chapterState(course, chapter, learn.courseScores[learn.openCourse]).passed) learn.levelRow++;
    }
}

void openLearnScreen(const LearnSetup& setup){
    learn.setup = setup;
    learn.instrument = setup.instrument;
    learn.pending = nullptr;
    learn.exercise.reset();
    refreshExercises();
}

void closeLearnScreen(){
    learn.exercise.reset();
}

bool learnBack(){
    if (learn.exercise){
        if (!learn.exercise->back()) endExercise();
        return false;
    }
    if (learn.openLevel >= 0){ // a level's chapters: back to the course's levels
        learn.openLevel = -1;
        return false;
    }
    if (learn.openCourse >= 0){ // a course's levels: back to the courses
        learn.openCourse = -1;
        return false;
    }
    if (!learn.openKind.empty()){ // inside a category: back to the drills
        learn.openKind.clear();
        return false;
    }
    return true;
}

// What a row of the Learn menu stands for
struct LearnRow {
    enum Kind { Heading, Lesson, Exercise, OpenExercises, OpenLessons, Course } kind;
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
    } else if (row.kind == LearnRow::Course){
        const CourseEntry& entry = learn.courses[row.index];
        title = entry.course.title;
        about = entry.course.description;
        progress = TextFormat("%d%%  ·  %d %s, %d chapters", coursePercent(entry.course, learn.courseScores[row.index]),
                              (int)entry.course.units.size(), entry.course.units.size() == 1 ? "level" : "levels", (int)entry.course.lessons.size());
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

// A row of a card list: a small label, a title, a line under it, a bar of how far it's come, a badge
struct CardRow {
    std::string label;
    std::string title;
    std::string subtitle;
    int percent = -1;        // -1: no bar
    const char* badge = nullptr;
    UiColor badgeColor = UiColor::Good;
    bool locked = false;
    bool highlight = false;  // the one to go on with
};

// Cards in a column, one chosen (Up/Down, the wheel, a click), scrolled to keep it in view; returns the one confirmed
// (Enter, or a click on the chosen one), or -1
static int cardList(const std::vector<CardRow>& rows, int& chosen, float& scroll, float x, float top, float width, float bottom, float rowHeight, float s){
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const UiFonts& fonts = uiFonts();
    const int count = (int)rows.size();
    if (count == 0) return -1;
    if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) chosen++;
    if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) chosen--;
    const float wheel = ImGui::GetIO().MouseWheel;
    if (wheel != 0.0f) chosen -= wheel > 0 ? 1 : -1;
    chosen = std::clamp(chosen, 0, count - 1);
    int confirmed = ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false) ? chosen : -1;
    const float view = bottom - top;
    const float target = std::clamp(chosen * rowHeight - view * 0.4f, 0.0f, std::max(0.0f, count * rowHeight - view));
    scroll = scroll < 0.0f ? target : scroll + (target - scroll) * std::min(1.0f, GetFrameTime() * 10.0f);
    const ImVec2 mouse = ImGui::GetMousePos();
    const bool click = ImGui::IsMouseClicked(ImGuiMouseButton_Left);
    draw->PushClipRect(ImVec2(x - 10 * s, top - 6 * s), ImVec2(x + width + 10 * s, bottom), true);
    for (int i = 0; i < count; i++){
        const CardRow& row = rows[i];
        const float rowTop = top + i * rowHeight - scroll;
        if (rowTop > bottom || rowTop + rowHeight < top) continue;
        const bool isChosen = i == chosen;
        const ImVec2 a(x, rowTop), b(x + width, rowTop + rowHeight - 12 * s);
        draw->AddRectFilled(a, b, isChosen ? uiColor(UiColor::Accent, 0.12f) : row.highlight ? uiColor(UiColor::Accent, 0.06f) : uiColor(UiColor::Card), 12 * s);
        draw->AddRect(a, b, uiColor(isChosen ? UiColor::Accent : row.highlight ? UiColor::Accent : UiColor::StaffLine, isChosen || !row.highlight ? 1.0f : 0.5f),
                      12 * s, 0, (isChosen ? 2.0f : 1.0f) * s);
        const UiColor ink = row.locked ? UiColor::Dim : UiColor::Ink;
        draw->AddText(fonts.mono, 12 * s, ImVec2(a.x + 20 * s, a.y + 12 * s), uiColor(row.highlight ? UiColor::Accent : UiColor::Dim),
                      (row.label + (row.locked ? "  ·  LOCKED" : "")).c_str());
        draw->AddText(fonts.bold, 24 * s, ImVec2(a.x + 20 * s, a.y + 28 * s), uiColor(ink), row.title.c_str());
        float lineY = a.y + 60 * s;
        if (!row.subtitle.empty()){
            draw->AddText(fonts.text, 16 * s, ImVec2(a.x + 20 * s, lineY), uiColor(UiColor::Dim), row.subtitle.c_str());
            lineY += 24 * s;
        }
        if (row.percent >= 0){
            const float barWidth = std::min(220 * s, width * 0.4f), barY = b.y - 18 * s;
            draw->AddRectFilled(ImVec2(a.x + 20 * s, barY), ImVec2(a.x + 20 * s + barWidth, barY + 3 * s), uiColor(UiColor::StaffLine), 2 * s);
            if (row.percent > 0)
                draw->AddRectFilled(ImVec2(a.x + 20 * s, barY), ImVec2(a.x + 20 * s + barWidth * row.percent / 100.0f, barY + 3 * s), uiColor(UiColor::Accent), 2 * s);
            float after = a.x + 20 * s + barWidth + 12 * s;
            draw->AddText(fonts.mono, 12 * s, ImVec2(after, barY - 7 * s), uiColor(UiColor::Dim), TextFormat("%d%%", row.percent));
            after += 50 * s;
            if (row.badge){
                const ImVec2 size = fonts.mono->CalcTextSizeA(11 * s, FLT_MAX, 0.0f, row.badge);
                draw->AddRectFilled(ImVec2(after, barY - 9 * s), ImVec2(after + size.x + 12 * s, barY + 9 * s), uiColor(row.badgeColor), 4 * s);
                draw->AddText(fonts.mono, 11 * s, ImVec2(after + 6 * s, barY - size.y / 2), uiColor(UiColor::Background), row.badge);
            }
        }
        if (click && mouse.x >= a.x && mouse.x <= b.x && mouse.y >= std::max(a.y, top) && mouse.y <= std::min(b.y, bottom)){
            if (isChosen) confirmed = i;
            chosen = i;
        }
    }
    draw->PopClipRect();
    return confirmed;
}

// A course: where to go on (its next chapter not passed), and its levels, each with how far it's come
static void courseLevels(){
    const int c = learn.openCourse;
    const Course& course = learn.courses[c].course;
    const CourseScores& scores = learn.courseScores[c];
    const float s = menuScale(), width = ImGui::GetWindowWidth(), height = ImGui::GetWindowHeight();
    menuScreenTitle(course.title.c_str(), s);
    const int next = courseContinue(course, scores);
    const CourseLesson& nextChapter = course.lessons[next];
    std::vector<CardRow> rows;
    CardRow resume;
    resume.label = coursePercent(course, scores) == 0 ? "START" : "CONTINUE";
    resume.title = nextChapter.lesson.title;
    resume.subtitle = TextFormat("Level %d, chapter %d: go on where you are", nextChapter.unit + 1, next - course.units[nextChapter.unit].firstLesson + 1);
    resume.highlight = true;
    rows.push_back(resume);
    for (int u = 0; u < (int)course.units.size(); u++){
        const CourseUnit& unit = course.units[u];
        CardRow row;
        row.label = TextFormat("LEVEL %d", u + 1);
        row.title = unit.title;
        row.subtitle = TextFormat("%d %s", unit.lessonCount, unit.lessonCount == 1 ? "chapter" : "chapters");
        row.percent = levelPercent(course, u, scores);
        bool perfect = true;
        for (int i = unit.firstLesson; i < unit.firstLesson + unit.lessonCount; i++) perfect = perfect && chapterState(course, i, scores).perfect;
        if (perfect) row.badge = "PERFECT", row.badgeColor = UiColor::Accent;
        row.locked = !chapterOpen(course, unit.firstLesson, scores);
        rows.push_back(row);
    }
    const float x = width * 0.07f;
    const int confirmed = cardList(rows, learn.courseRow, learn.courseScroll, x, height * 0.2f, width * 0.6f, height * 0.92f, 128 * s, s);
    // Beside it, the course's own words
    if (!course.description.empty())
        ImGui::GetWindowDrawList()->AddText(uiFonts().text, 17 * s, ImVec2(width * 0.71f, height * 0.2f), uiColor(UiColor::Dim), course.description.c_str(),
                                            nullptr, width * 0.22f);
    menuScreenHint("Up/Down  choose    Enter  open    Esc  back to the courses", s);
    if (confirmed == 0){
        learn.openLevel = nextChapter.unit;
        learn.levelRow = next - course.units[nextChapter.unit].firstLesson;
        learn.levelScroll = -1.0f;
        countPlay(learn.courses[c].id + "-" + nextChapter.id);
        startWhenTuned(true, [c, next](){ return openCourseChapter(c, next); });
    } else if (confirmed > 0){
        const int u = confirmed - 1;
        learn.openLevel = u;
        learn.levelRow = 0;
        for (int i = 0; i < course.units[u].lessonCount; i++){ // its first chapter not passed
            learn.levelRow = i;
            if (!chapterState(course, course.units[u].firstLesson + i, scores).passed) break;
        }
        learn.levelScroll = -1.0f;
    }
}

// A level: its chapters, each with how far it's come, passed or perfect, or locked until the one before is passed
static void levelChapters(){
    const int c = learn.openCourse;
    const Course& course = learn.courses[c].course;
    const CourseScores& scores = learn.courseScores[c];
    const CourseUnit& unit = course.units[learn.openLevel];
    const float s = menuScale(), width = ImGui::GetWindowWidth(), height = ImGui::GetWindowHeight();
    menuScreenTitle(TextFormat("Level %d: %s", learn.openLevel + 1, unit.title.c_str()), s);
    const int next = courseContinue(course, scores);
    std::vector<CardRow> rows;
    for (int i = 0; i < unit.lessonCount; i++){
        const int lesson = unit.firstLesson + i;
        const ChapterState state = chapterState(course, lesson, scores);
        const int drills = (int)courseDrills(course, lesson).size();
        CardRow row;
        row.label = TextFormat("CHAPTER %d", i + 1);
        row.title = course.lessons[lesson].lesson.title;
        row.subtitle = drills == 0 ? "to read" : TextFormat("%d %s", drills, drills == 1 ? "drill" : "drills");
        row.percent = state.percent;
        if (state.perfect) row.badge = "PERFECT", row.badgeColor = UiColor::Accent;
        else if (state.passed) row.badge = "PASSED";
        row.locked = !chapterOpen(course, lesson, scores);
        row.highlight = lesson == next && !state.passed;
        rows.push_back(row);
    }
    const int confirmed = cardList(rows, learn.levelRow, learn.levelScroll, width * 0.07f, height * 0.2f, width * 0.6f, height * 0.92f, 128 * s, s);
    if (rows[learn.levelRow].locked)
        ImGui::GetWindowDrawList()->AddText(uiFonts().text, 17 * s, ImVec2(width * 0.71f, height * 0.2f), uiColor(UiColor::Dim),
                                            "Locked: pass the chapter before it to open it.", nullptr, width * 0.22f);
    menuScreenHint("Up/Down  choose    Enter  open    Esc  back to the levels", s);
    if (confirmed >= 0 && !rows[confirmed].locked){
        const int lesson = unit.firstLesson + confirmed;
        countPlay(learn.courses[c].id + "-" + course.lessons[lesson].id);
        startWhenTuned(true, [c, lesson](){ return openCourseChapter(c, lesson); });
    }
}

// Learn is in three sections, switched beside each other under the title: the lessons (each a path through steps), the
// drills (by category: Enter opens one, its exercises with their progress) and the games. Each section keeps its own
// place in its list.
static const char* const SECTION_NAMES[] = { "COURSES", "DRILLS", "GAMES" };
enum LearnSection { SectionLessons, SectionDrills, SectionGames, SECTION_COUNT };
static const char* const GAMES_CATEGORY = "Games"; // the exercises of this category are the games

static void exerciseMenu(){
    if (learn.openCourse >= 0 && learn.openCourse < (int)learn.courses.size()){
        if (learn.openLevel >= 0 && learn.openLevel < (int)learn.courses[learn.openCourse].course.units.size()) levelChapters();
        else courseLevels();
        return;
    }
    learn.openCourse = -1;
    float s = menuScale();
    const bool inKind = !learn.openKind.empty();
    menuScreenTitle(inKind ? learn.openKind.c_str() : "Learn", s);
    float width = ImGui::GetWindowWidth(), height = ImGui::GetWindowHeight();
    const MenuListArea area = { ImVec2(width * 0.07f, height * 0.24f), width * 0.48f, height * 0.68f - 20 * s, s };
    std::vector<MenuRow> rows;
    std::vector<LearnRow> targets;
    std::vector<std::string> kinds; // in Drills: each row's category ("" for the others)
    auto add = [&](const MenuRow& row, LearnRow target, const std::string& kind = ""){
        rows.push_back(row);
        targets.push_back(target);
        kinds.push_back(kind);
    };
    // A lesson's or an exercise's progress, how many times it was done, and whether it's the player's own
    auto detail = [](std::string progress, const std::string& id, bool builtIn){
        const std::string plays = playsText(id);
        if (!plays.empty()) progress += (progress.empty() ? "" : "  ·  ") + plays;
        if (!builtIn) progress += progress.empty() ? "yours" : "  ·  yours";
        return progress;
    };
    auto exerciseRow = [&](int i){
        const ExerciseEntry& entry = learn.exercises[i];
        MenuRow row;
        row.label = entry.exercise.title;
        row.detail = detail(learn.progressText[i], entry.id, entry.builtIn);
        row.note = entry.error;
        row.disabled = !entry.error.empty();
        add(row, { LearnRow::Exercise, i });
    };
    auto folderRows = [&](bool lessons){
        MenuRow folders;
        folders.label = "YOUR OWN";
        folders.heading = true;
        add(folders, { LearnRow::Heading, -1 });
        if (lessons) add(actionRow("Open lessons folder"), { LearnRow::OpenLessons, -1 });
        else add(actionRow("Open exercises folder"), { LearnRow::OpenExercises, -1 });
    };

    MenuList* list = &learn.list;
    if (inKind){
        // Inside a category of drills: its exercises, with their progress and how many times each was done
        for (int i = 0; i < (int)learn.exercises.size(); i++)
            if (learn.exercises[i].exercise.category == learn.openKind && forInstrument(learn.exercises[i].exercise)) exerciseRow(i);
        if (rows.empty()){ // the category is gone (its files were removed meanwhile)
            learn.openKind.clear();
            return;
        }
    } else {
        // Beside the title: learning, or making lessons (Tab)
        const char* const modes[] = { "LEARN", "EDIT LESSONS" };
        int mode = 0;
        menuSwitchRow("MODE", modes, 2, mode, width * 0.55f, height * 0.09f + 14 * s, s);
        wantsEditor = mode == 1 || ImGui::IsKeyPressed(ImGuiKey_Tab);
        // Under it, the instrument played: its courses and exercises (I switches)
        const char* const instruments[] = { "GUITAR", "BASS" };
        int played = learn.instrument == InputRole::Bass ? 1 : 0;
        const bool clicked = menuSwitchRow("INSTRUMENT", instruments, 2, played, width * 0.55f, height * 0.165f, s);
        if (clicked || ImGui::IsKeyPressed(ImGuiKey_I, false)){
            learn.instrument = (clicked ? played == 1 : learn.instrument == InputRole::Guitar) ? InputRole::Bass : InputRole::Guitar;
            learn.instrumentChanged = true;
            for (MenuList& sectionList : learn.sections) sectionList.selected = -1; // the lists changed
        }
        // Under it, the sections: clicked, or Left and Right
        int section = learn.section;
        menuSwitchRow("SECTION", SECTION_NAMES, SECTION_COUNT, section, width * 0.07f, height * 0.165f, s);
        if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow)) section = std::max(0, section - 1);
        if (ImGui::IsKeyPressed(ImGuiKey_RightArrow)) section = std::min(SECTION_COUNT - 1, section + 1);
        learn.section = section;
        list = &learn.sections[section];
        if (section == SectionLessons){
            // The courses, a path each, then the lessons on their own (the player's)
            for (int i = 0; i < (int)learn.courses.size(); i++){
                const CourseEntry& entry = learn.courses[i];
                if (!forInstrument(entry.course)) continue;
                MenuRow row;
                row.label = entry.course.title;
                row.detail = TextFormat("%d%%  ·  %d chapters", coursePercent(entry.course, learn.courseScores[i]), (int)entry.course.lessons.size());
                row.note = entry.error;
                row.disabled = !entry.error.empty();
                add(row, { LearnRow::Course, i });
            }
            if (!learn.lessons.empty()){
                MenuRow heading;
                heading.label = "LESSONS";
                heading.heading = true;
                add(heading, { LearnRow::Heading, -1 });
            }
            for (int i = 0; i < (int)learn.lessons.size(); i++){
                const LessonEntry& entry = learn.lessons[i];
                MenuRow row;
                row.label = entry.lesson.title;
                row.detail = detail(learn.lessonProgressText[i], entry.id, entry.builtIn);
                row.note = entry.error;
                row.disabled = !entry.error.empty();
                add(row, { LearnRow::Lesson, i });
            }
            folderRows(true);
        } else if (section == SectionDrills){
            // The categories, each with how many exercises and how often they've been done
            for (size_t i = 0; i < learn.exercises.size();){
                const std::string& category = learn.exercises[i].exercise.category;
                int count = 0, times = 0;
                for (; i < learn.exercises.size() && learn.exercises[i].exercise.category == category; i++){
                    if (!forInstrument(learn.exercises[i].exercise)) continue;
                    count++;
                    times += learn.plays.count(learn.exercises[i].id) ? learn.plays[learn.exercises[i].id].times : 0;
                }
                if (category == GAMES_CATEGORY || count == 0) continue;
                MenuRow row;
                row.label = category;
                row.detail = TextFormat("%d exercise%s", count, count == 1 ? "" : "s");
                if (times > 0) row.detail += TextFormat("  ·  done %d %s", times, times == 1 ? "time" : "times");
                add(row, { LearnRow::Heading, -1 }, category);
            }
            folderRows(false);
        } else {
            for (int i = 0; i < (int)learn.exercises.size(); i++)
                if (learn.exercises[i].exercise.category == GAMES_CATEGORY && forInstrument(learn.exercises[i].exercise)) exerciseRow(i);
            folderRows(false);
        }
    }

    int confirmed = menuList(*list, rows, area);
    if (list->selected >= 0 && list->selected < (int)targets.size()) drawAbout(targets[list->selected], s);
    menuScreenHint(inKind ? "Up/Down  choose    Enter  start    Esc  back to the drills"
                          : "Left/Right  section    Up/Down  choose    Enter  open    I  guitar or bass    Tab  edit lessons    Esc  back", s);
    if (confirmed < 0) return;
    const LearnRow& target = targets[confirmed];
    if (!kinds[confirmed].empty()){
        learn.openKind = kinds[confirmed];
        learn.list.selected = -1; // its first
    } else if (target.kind == LearnRow::Lesson){
        countPlay(learn.lessons[target.index].id);
        learn.exercise = openLesson(learn.lessons[target.index]);
    } else if (target.kind == LearnRow::Exercise){
        countPlay(learn.exercises[target.index].id);
        const ExerciseEntry entry = learn.exercises[target.index];
        startWhenTuned(exercisePlayedOnInstrument(entry.exercise), [entry](){ return createExercise(entry); });
    } else if (target.kind == LearnRow::Course){
        learn.openCourse = target.index;
        learn.openLevel = -1;
        learn.courseRow = 0; // Continue
        learn.courseScroll = -1.0f; // placed at once, not eased
    } else if (target.kind == LearnRow::OpenExercises){
        openFolder(learn.setup.userExercises);
    } else if (target.kind == LearnRow::OpenLessons){
        openFolder(learn.setup.userLessons);
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
