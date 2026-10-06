#include "screens/learnscreen.h"

#include "core/course.h"
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
    std::vector<std::vector<bool>> courseDone; // each course's lessons: done
    int openCourse = -1;                    // the course whose path is open; -1 for none
    int courseSelected = 0;                 // its lesson chosen on the path
    float courseScroll = 0.0f;              // how far the path is scrolled up (pixels), easing to keep the choice in view
    double courseOpenedAt = -100.0;
    MenuList list;                          // the open category's list: its selection, kept while exercises run
    MenuList sections[3];                   // each section's list (courses and lessons, drills, games)
    int section = 0;                        // the section shown
    std::string openKind;                   // the category of drills whose list is open; "" for none
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
    learn.courseDone.clear();
    for (CourseEntry& entry : learn.courses){
        std::vector<bool> done;
        for (const CourseLesson& lesson : entry.course.lessons){
            done.push_back(loadLessonProgress(progressPath(entry.id + "-" + lesson.id)).completed);
            // A named exercise must be there
            for (const LessonStep& step : lesson.lesson.steps)
                if (entry.error.empty() && step.type == LessonStepType::Exercise && !step.inlined && !findExercise(learn.exercises, true, step.exercise))
                    entry.error = "the lesson '" + lesson.lesson.title + "' uses the exercise '" + step.exercise + "', which isn't there";
        }
        learn.courseDone.push_back(done);
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

// A course's lesson, played like any lesson: its exercises written in place each kept by the lesson and step
static std::unique_ptr<Exercise> openCourseLesson(int courseIndex, int lessonIndex){
    const CourseEntry& course = learn.courses[courseIndex];
    const CourseLesson& lesson = course.course.lessons[lessonIndex];
    LessonEntry entry;
    entry.folder = std::filesystem::path(course.path).parent_path().string();
    entry.id = course.id + "-" + lesson.id;
    entry.builtIn = true;
    entry.lesson = lesson.lesson;
    std::vector<ExerciseEntry> stepExercises;
    for (size_t i = 0; i < lesson.lesson.steps.size(); i++){
        const LessonStep& step = lesson.lesson.steps[i];
        ExerciseEntry exercise;
        if (step.type == LessonStepType::Exercise && step.inlined){
            exercise.name = lesson.id + "-" + std::to_string(i + 1);
            exercise.id = entry.id + "-" + std::to_string(i + 1);
            exercise.builtIn = true;
            exercise.exercise = step.inlineExercise;
        } else if (step.type == LessonStepType::Exercise){
            if (const ExerciseEntry* found = findExercise(learn.exercises, true, step.exercise)) exercise = *found;
        }
        stepExercises.push_back(exercise);
    }
    return std::make_unique<LessonPlayer>(entry, stepExercises, createExercise, lessonPlayOptions(), progressPath(entry.id));
}

// A course's lesson is open once the one before it is done (the first always)
static bool courseLessonOpen(int courseIndex, int lessonIndex){
    return lessonIndex == 0 || learn.courseDone[courseIndex][lessonIndex - 1];
}

// The lesson to go on with: the first not done
static int courseNext(int courseIndex){
    const std::vector<bool>& done = learn.courseDone[courseIndex];
    for (int i = 0; i < (int)done.size(); i++) if (!done[i]) return i;
    return std::max(0, (int)done.size() - 1);
}

static void endExercise(){
    learn.exercise.reset();
    refreshExercises(); // new progress to show, and files may have been edited meanwhile
    // Back on a course's path: on to its next lesson, if the one played is done now
    if (learn.openCourse >= 0 && learn.openCourse < (int)learn.courses.size()){
        const int c = learn.openCourse;
        if (learn.courseSelected < (int)learn.courseDone[c].size() && learn.courseDone[c][learn.courseSelected]) learn.courseSelected = courseNext(c);
    }
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
        if (!learn.exercise->back()) endExercise();
        return false;
    }
    if (learn.openCourse >= 0){ // on a course's path: back to the courses
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
        const int done = (int)std::count(learn.courseDone[row.index].begin(), learn.courseDone[row.index].end(), true);
        progress = TextFormat("%d of %d lessons done, in %d %s", done, (int)entry.course.lessons.size(), (int)entry.course.units.size(),
                              entry.course.units.size() == 1 ? "unit" : "units");
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

// A tick, a play triangle, a padlock: drawn, so any font will do
static void drawTick(ImDrawList* draw, ImVec2 at, float size, ImU32 color){
    draw->AddLine(ImVec2(at.x - size * 0.5f, at.y), ImVec2(at.x - size * 0.12f, at.y + size * 0.4f), color, size * 0.22f);
    draw->AddLine(ImVec2(at.x - size * 0.12f, at.y + size * 0.4f), ImVec2(at.x + size * 0.55f, at.y - size * 0.4f), color, size * 0.22f);
}
static void drawPlay(ImDrawList* draw, ImVec2 at, float size, ImU32 color){
    draw->AddTriangleFilled(ImVec2(at.x - size * 0.35f, at.y - size * 0.5f), ImVec2(at.x - size * 0.35f, at.y + size * 0.5f),
                            ImVec2(at.x + size * 0.55f, at.y), color);
}
static void drawLock(ImDrawList* draw, ImVec2 at, float size, ImU32 color){
    draw->AddRectFilled(ImVec2(at.x - size * 0.45f, at.y - size * 0.05f), ImVec2(at.x + size * 0.45f, at.y + size * 0.55f), color, size * 0.1f);
    draw->PathArcTo(ImVec2(at.x, at.y - size * 0.05f), size * 0.3f, 3.14159f, 6.28318f, 12);
    draw->PathStroke(color, 0, size * 0.14f);
}

// A course's path, Duolingo-style: its units, and their lessons as stops along a winding line, the done ones green,
// the next one lit, the ones after it closed until it's done (a done one can always be played again). Up and Down
// choose, Enter starts; a card beside it says what the chosen lesson holds.
static void coursePath(){
    const int c = learn.openCourse;
    if (c < 0 || c >= (int)learn.courses.size() || learn.courses[c].course.lessons.empty()){
        learn.openCourse = -1;
        return;
    }
    const Course& course = learn.courses[c].course;
    const std::vector<bool>& done = learn.courseDone[c];
    const float s = menuScale(), width = ImGui::GetWindowWidth(), height = ImGui::GetWindowHeight();
    menuScreenTitle(course.title.c_str(), s);
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const UiFonts& fonts = uiFonts();
    const int count = (int)course.lessons.size(), next = courseNext(c);
    int& chosen = learn.courseSelected;
    if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) chosen = std::min(count - 1, chosen + 1);
    if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) chosen = std::max(0, chosen - 1);
    const float wheel = ImGui::GetIO().MouseWheel;
    if (wheel != 0.0f) chosen = std::clamp(chosen - (wheel > 0 ? 1 : -1), 0, count - 1);
    chosen = std::clamp(chosen, 0, count - 1);
    bool start = ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false);

    // Where everything goes, top to bottom: a unit's heading, its lessons
    const float areaTop = height * 0.17f, areaBottom = height * 0.9f, left = width * 0.07f;
    const float unitHeight = 70 * s, lessonHeight = 80 * s;
    std::vector<float> lessonY(count), unitY(course.units.size());
    float y = 0.0f;
    for (size_t u = 0; u < course.units.size(); u++){
        unitY[u] = y;
        y += unitHeight;
        for (int i = course.units[u].firstLesson; i < course.units[u].firstLesson + course.units[u].lessonCount; i++){
            lessonY[i] = y + lessonHeight / 2;
            y += lessonHeight;
        }
        y += 10 * s;
    }
    // Scrolled to keep the chosen lesson two fifths of the way down
    const float view = areaBottom - areaTop;
    const float target = std::clamp(lessonY[chosen] - view * 0.4f, 0.0f, std::max(0.0f, y - view));
    if (learn.courseScroll < 0.0f) learn.courseScroll = target;
    else learn.courseScroll += (target - learn.courseScroll) * std::min(1.0f, GetFrameTime() * 10.0f);
    const float scroll = learn.courseScroll;

    const float pathX = left + 110 * s, labelX = pathX + 130 * s;
    auto nodeAt = [&](int i){ return ImVec2(pathX + std::sin(i * 0.9f) * 60 * s, areaTop + lessonY[i] - scroll); };
    const float pulse = 0.5f + 0.5f * std::sin((float)GetTime() * 4.0f);
    draw->PushClipRect(ImVec2(0, areaTop), ImVec2(width * 0.56f, areaBottom), true);
    // The line between the stops, green where it's been walked
    for (int i = 0; i + 1 < count; i++)
        draw->AddLine(nodeAt(i), nodeAt(i + 1), done[i] ? uiColor(UiColor::Good, 0.55f) : uiColor(UiColor::StaffLine), 6 * s);
    for (size_t u = 0; u < course.units.size(); u++){
        const float top = areaTop + unitY[u] - scroll;
        draw->AddText(fonts.mono, 13 * s, ImVec2(left, top + 10 * s), uiColor(UiColor::Accent), TextFormat("UNIT %d", (int)u + 1));
        draw->AddText(fonts.bold, 24 * s, ImVec2(left, top + 28 * s), uiColor(UiColor::Ink), course.units[u].title.c_str());
    }
    const ImVec2 mouse = ImGui::GetMousePos();
    const bool click = ImGui::IsMouseClicked(ImGuiMouseButton_Left);
    for (int i = 0; i < count; i++){
        const ImVec2 at = nodeAt(i);
        if (at.y < areaTop - 60 * s || at.y > areaBottom + 60 * s) continue;
        const bool isDone = done[i], open = courseLessonOpen(c, i), isNext = i == next && !isDone;
        const float r = (isNext ? 27.0f : 22.0f) * s;
        if (isNext) draw->AddCircle(at, r + 6 * s + 5 * s * pulse, uiColor(UiColor::Accent, 0.5f * (1.0f - pulse) + 0.2f), 40, 3 * s);
        const ImU32 fill = isDone ? uiColor(UiColor::Good) : isNext ? uiColor(UiColor::Accent) : open ? uiColor(UiColor::Ink, 0.85f) : uiColor(UiColor::Card);
        draw->AddCircleFilled(at, r, fill, 40);
        if (!open && !isDone) draw->AddCircle(at, r, uiColor(UiColor::StaffLine), 40, 2 * s);
        if (isDone) drawTick(draw, at, r * 0.8f, uiColor(UiColor::Background));
        else if (open) drawPlay(draw, at, r * 0.7f, uiColor(UiColor::Background));
        else drawLock(draw, ImVec2(at.x, at.y - r * 0.1f), r * 0.75f, uiColor(UiColor::Dim));
        if (i == chosen) draw->AddCircle(at, r + 4 * s, uiColor(UiColor::Ink), 40, 2.5f * s);
        // Its name and how it stands
        const char* state = isDone ? "done" : isNext ? "start here" : open ? "open" : "locked";
        const UiColor stateColor = isDone ? UiColor::Good : isNext ? UiColor::Accent : UiColor::Dim;
        draw->AddText(fonts.bold, 20 * s, ImVec2(labelX, at.y - 20 * s), uiColor(open || isDone ? UiColor::Ink : UiColor::Dim),
                      course.lessons[i].lesson.title.c_str());
        draw->AddText(fonts.mono, 13 * s, ImVec2(labelX, at.y + 6 * s), uiColor(stateColor), state);
        // A click chooses it; on the chosen one, starts it
        const float dx = mouse.x - at.x, dy = mouse.y - at.y;
        const bool over = (dx * dx + dy * dy <= (r + 6 * s) * (r + 6 * s))
                          || (mouse.x >= labelX && mouse.x <= labelX + 360 * s && std::abs(dy) <= lessonHeight / 2);
        if (click && over && mouse.y >= areaTop && mouse.y <= areaBottom){
            if (i == chosen) start = true;
            chosen = i;
        }
    }
    draw->PopClipRect();

    // The chosen lesson, on a card: what it is, what's in it, and whether it can be played yet
    const CourseLesson& lesson = course.lessons[chosen];
    int reading = 0, playing = 0;
    for (const LessonStep& step : lesson.lesson.steps) (step.type == LessonStepType::Text ? reading : playing)++;
    const float cardX = width * 0.58f, cardY = height * 0.22f, cardWidth = width * 0.35f, pad = 26 * s;
    const bool open = courseLessonOpen(c, chosen);
    std::string steps = TextFormat("%d %s", (int)lesson.lesson.steps.size(), lesson.lesson.steps.size() == 1 ? "step" : "steps");
    if (reading && playing) steps += TextFormat(": %d to read, %d to play", reading, playing);
    else if (playing) steps += ", all to play";
    const std::string state = done[chosen] ? "Done. Enter to play it again." : open ? "Enter to start." : "Locked: finish \"" + course.lessons[chosen - 1].lesson.title + "\" first.";
    const float inner = cardWidth - 2 * pad;
    const float titleHeight = fonts.bold->CalcTextSizeA(26 * s, FLT_MAX, inner, lesson.lesson.title.c_str()).y;
    const float cardHeight = pad * 2 + 18 * s + titleHeight + 14 * s + 22 * s + 30 * s;
    draw->AddRectFilled(ImVec2(cardX, cardY), ImVec2(cardX + cardWidth, cardY + cardHeight), uiColor(UiColor::Card), 10 * s);
    float textY = cardY + pad;
    draw->AddText(fonts.mono, 13 * s, ImVec2(cardX + pad, textY), uiColor(UiColor::Dim),
                  TextFormat("UNIT %d  ·  LESSON %d OF %d", lesson.unit + 1, chosen + 1, count));
    textY += 18 * s;
    draw->AddText(fonts.bold, 26 * s, ImVec2(cardX + pad, textY), uiColor(UiColor::Ink), lesson.lesson.title.c_str(), nullptr, inner);
    textY += titleHeight + 14 * s;
    draw->AddText(fonts.text, 18 * s, ImVec2(cardX + pad, textY), uiColor(UiColor::Dim), steps.c_str());
    textY += 30 * s;
    draw->AddText(fonts.bold, 18 * s, ImVec2(cardX + pad, textY), uiColor(done[chosen] ? UiColor::Good : open ? UiColor::Accent : UiColor::Dim),
                  state.c_str(), nullptr);
    if (!course.description.empty())
        draw->AddText(fonts.text, 16 * s, ImVec2(cardX, cardY + cardHeight + 20 * s), uiColor(UiColor::Dim), course.description.c_str(), nullptr, cardWidth);

    menuScreenHint("Up/Down  choose    Enter  start    Esc  back to the courses", s);
    if (start && open){
        countPlay(learn.courses[c].id + "-" + lesson.id);
        learn.exercise = openCourseLesson(c, chosen);
    }
}

// Learn is in three sections, switched beside each other under the title: the lessons (each a path through steps), the
// drills (by category: Enter opens one, its exercises with their progress) and the games. Each section keeps its own
// place in its list.
static const char* const SECTION_NAMES[] = { "COURSES", "DRILLS", "GAMES" };
enum LearnSection { SectionLessons, SectionDrills, SectionGames, SECTION_COUNT };
static const char* const GAMES_CATEGORY = "Games"; // the exercises of this category are the games

static void exerciseMenu(){
    if (learn.openCourse >= 0){
        coursePath();
        return;
    }
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
        for (int i = 0; i < (int)learn.exercises.size(); i++) if (learn.exercises[i].exercise.category == learn.openKind) exerciseRow(i);
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
                MenuRow row;
                row.label = entry.course.title;
                const int done = (int)std::count(learn.courseDone[i].begin(), learn.courseDone[i].end(), true);
                row.detail = TextFormat("%d of %d lessons", done, (int)entry.course.lessons.size());
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
                    count++;
                    times += learn.plays.count(learn.exercises[i].id) ? learn.plays[learn.exercises[i].id].times : 0;
                }
                if (category == GAMES_CATEGORY) continue;
                MenuRow row;
                row.label = category;
                row.detail = TextFormat("%d exercise%s", count, count == 1 ? "" : "s");
                if (times > 0) row.detail += TextFormat("  ·  done %d %s", times, times == 1 ? "time" : "times");
                add(row, { LearnRow::Heading, -1 }, category);
            }
            folderRows(false);
        } else {
            for (int i = 0; i < (int)learn.exercises.size(); i++) if (learn.exercises[i].exercise.category == GAMES_CATEGORY) exerciseRow(i);
            folderRows(false);
        }
    }

    int confirmed = menuList(*list, rows, area);
    if (list->selected >= 0 && list->selected < (int)targets.size()) drawAbout(targets[list->selected], s);
    menuScreenHint(inKind ? "Up/Down  choose    Enter  start    Esc  back to the drills"
                          : "Left/Right  section    Up/Down  choose    Enter  open    Tab  edit lessons    Esc  back", s);
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
        learn.exercise = createExercise(learn.exercises[target.index]);
    } else if (target.kind == LearnRow::Course){
        learn.openCourse = target.index;
        learn.courseSelected = courseNext(target.index);
        learn.courseScroll = -1.0f; // placed at once, not eased
        learn.courseOpenedAt = GetTime();
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
