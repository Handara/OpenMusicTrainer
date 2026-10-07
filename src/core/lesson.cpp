#include "core/lesson.h"

#include "core/chart.h"
#include "core/files.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

const int SUPPORTED_LESSON_VERSION = 1;
const int DEFAULT_CLEAN_PASSES = 1;
const int DEFAULT_ANSWERS_IN_A_ROW = 5;
const int DEFAULT_PLAY_PERCENT = 80;

struct StepTypeInfo {
    LessonStepType type;
    const char* name;
};
const StepTypeInfo STEP_TYPES[] = {
    {LessonStepType::Text, "text"}, {LessonStepType::Image, "image"}, {LessonStepType::Audio, "audio"},
    {LessonStepType::Video, "video"}, {LessonStepType::Exercise, "exercise"}, {LessonStepType::Play, "play"},
};

const char* lessonStepTypeName(LessonStepType type){
    for (const StepTypeInfo& info : STEP_TYPES) if (info.type == type) return info.name;
    return "text";
}

const std::vector<std::string>& lessonFileExtensions(LessonStepType type){
    static const std::vector<std::string> none;
    static const std::vector<std::string> images = {".png", ".jpg", ".jpeg"};
    static const std::vector<std::string> sounds = {".wav", ".flac", ".mp3"};
    static const std::vector<std::string> videos = {".mpg", ".mpeg"}; // MPEG-1: see the video player
    static const std::vector<std::string> charts = {".chart"};
    switch (type){
        case LessonStepType::Image: return images;
        case LessonStepType::Audio: return sounds;
        case LessonStepType::Video: return videos;
        case LessonStepType::Play:  return charts;
        default:                    return none;
    }
}

int lessonGoal(const LessonStep& step, ExerciseType exerciseType){
    if (step.goal > 0) return step.goal;
    if (step.type == LessonStepType::Play) return DEFAULT_PLAY_PERCENT;
    bool drill = exerciseType == ExerciseType::Scale || exerciseType == ExerciseType::Rhythm || exerciseType == ExerciseType::Reading
                 || exerciseType == ExerciseType::Chords || exerciseType == ExerciseType::Notes || exerciseType == ExerciseType::Neck
                 || exerciseType == ExerciseType::NeckWalk;
    return drill ? DEFAULT_CLEAN_PASSES : DEFAULT_ANSWERS_IN_A_ROW;
}

// "an image", "a video": for error messages
static std::string withArticle(LessonStepType type){
    std::string name = lessonStepTypeName(type);
    return (std::string("aeiou").find(name[0]) != std::string::npos ? "an " : "a ") + name;
}

static std::string lowercaseExtension(const std::string& file){
    std::string extension = fs::path(file).extension().string();
    for (char& c : extension) c = (char)std::tolower((unsigned char)c);
    return extension;
}

// Which keys a step type accepts, besides its title
static bool stepAccepts(LessonStepType type, const std::string& key){
    bool hasFile = !lessonFileExtensions(type).empty();
    bool hasCaption = type == LessonStepType::Image || type == LessonStepType::Audio || type == LessonStepType::Video;
    if (key == "text") return type == LessonStepType::Text;
    if (key == "file") return hasFile;
    if (key == "caption") return hasCaption;
    if (key == "exercise") return type == LessonStepType::Exercise;
    if (key == "goal") return type == LessonStepType::Exercise || type == LessonStepType::Play;
    return false;
}

bool loadLesson(const std::string& folder, Lesson& out, std::string& error){
    std::string path = (fs::path(folder) / LESSON_FILE_NAME).string();
    std::ifstream file(path);
    if (!file){
        error = path + ": could not open file";
        return false;
    }
    out = Lesson{};
    int lineNumber = 0;
    auto lineError = [&](const std::string& message){
        error = path + ":" + std::to_string(lineNumber) + ": " + message;
        return false;
    };

    int version = 0;
    std::vector<int> stepLines; // where each step starts, for its errors
    std::string text;
    while (std::getline(file, text)){
        lineNumber++;
        if (!text.empty() && text.back() == '\r') text.pop_back();
        std::istringstream ss(text);
        std::string key;
        if (!(ss >> key) || key[0] == '#') continue;
        std::string rest;
        std::getline(ss >> std::ws, rest);
        while (!rest.empty() && std::isspace((unsigned char)rest.back())) rest.pop_back();

        if (key == "version"){
            std::istringstream number(rest);
            if (!(number >> version) || !(number >> std::ws).eof()) return lineError("expected: version <number>");
            continue;
        }
        if (key == "step"){
            bool known = false;
            LessonStep step;
            for (const StepTypeInfo& info : STEP_TYPES) if (rest == info.name){ step.type = info.type; known = true; }
            if (!known) return lineError("unknown step type '" + rest + "' (known: text, image, audio, video, exercise, play)");
            out.steps.push_back(step);
            stepLines.push_back(lineNumber);
            continue;
        }
        if (out.steps.empty()){
            // The header, before the first step
            if (key == "title") out.title = rest;
            else if (key == "category"){ if (!rest.empty()) out.category = rest; }
            else if (key == "author") out.author = rest;
            else if (key == "description") out.description = rest;
            else return lineError("unknown setting '" + key + "' before the first step");
            continue;
        }

        LessonStep& step = out.steps.back();
        if (key == "title"){ step.title = rest; continue; }
        if (!stepAccepts(step.type, key)) return lineError("'" + key + "' doesn't belong in " + withArticle(step.type) + " step");
        if (rest.empty()) return lineError("'" + key + "' needs a value");
        if (key == "text") step.paragraphs.push_back(rest);
        else if (key == "file") step.file = rest;
        else if (key == "caption") step.caption = rest;
        else if (key == "exercise") step.exercise = rest;
        else if (key == "goal"){
            std::istringstream number(rest);
            int goal = 0;
            bool ok = (bool)(number >> goal);
            if (ok && step.type == LessonStepType::Play && number.peek() == '%') number.get(); // "90" or "90%"
            ok = ok && (number >> std::ws).eof() && goal >= 1 && (step.type != LessonStepType::Play || goal <= 100);
            if (!ok){
                return lineError(step.type == LessonStepType::Play ? "goal is a percentage of notes hit, 1 to 100"
                                                                  : "goal is a count, 1 or more");
            }
            step.goal = goal;
        }
    }

    if (version == 0){ error = path + ": missing 'version'"; return false; }
    if (version > SUPPORTED_LESSON_VERSION){
        error = path + ": lesson format v" + std::to_string(version) + " is newer than this build supports (v"
              + std::to_string(SUPPORTED_LESSON_VERSION) + ")";
        return false;
    }
    if (out.title.empty()){ error = path + ": missing 'title'"; return false; }
    if (out.steps.empty()){ error = path + ": a lesson needs at least one step"; return false; }

    // Each step has what it needs, and its file is really there
    for (size_t i = 0; i < out.steps.size(); i++){
        const LessonStep& step = out.steps[i];
        lineNumber = stepLines[i];
        const std::vector<std::string>& extensions = lessonFileExtensions(step.type);
        if (step.type == LessonStepType::Text && step.paragraphs.empty() && step.title.empty()) return lineError("a text step needs a title or some text");
        if (step.type == LessonStepType::Exercise && step.exercise.empty()) return lineError("an exercise step needs 'exercise <file name>'");
        if (extensions.empty()) continue;
        if (step.file.empty()) return lineError(withArticle(step.type) + " step needs 'file <name>'");
        // Only a plain name: a shared lesson must not reach outside its own folder
        if (step.file.find_first_of("/\\") != std::string::npos || step.file == "." || step.file == "..") return lineError("'" + step.file + "' must be a file in the lesson's folder");
        if (std::find(extensions.begin(), extensions.end(), lowercaseExtension(step.file)) == extensions.end()){
            std::string list;
            for (const std::string& extension : extensions) list += (list.empty() ? "" : ", ") + extension;
            return lineError("'" + step.file + "' isn't " + withArticle(step.type) + " file (" + list + ")");
        }
        fs::path media = fs::path(folder) / step.file;
        if (!fs::is_regular_file(media)) return lineError("'" + step.file + "' isn't in the lesson's folder");
        if (step.type == LessonStepType::Play){
            Chart chart;
            std::string chartError;
            if (!loadChart(media.string(), chart, chartError)) return lineError(chartError);
        }
    }
    return true;
}

// One value per line: a line break inside one would start a new, unintended line in the file
static std::string oneLine(const std::string& value){
    std::string line = value;
    std::replace(line.begin(), line.end(), '\n', ' ');
    std::replace(line.begin(), line.end(), '\r', ' ');
    return line;
}

bool saveLesson(const std::string& folder, const Lesson& lesson, std::string& error){
    std::ostringstream out;
    out << "# hardthz lesson\n";
    out << "version " << SUPPORTED_LESSON_VERSION << "\n";
    out << "title " << oneLine(lesson.title) << "\n";
    out << "category " << oneLine(lesson.category) << "\n";
    if (!lesson.author.empty()) out << "author " << oneLine(lesson.author) << "\n";
    if (!lesson.description.empty()) out << "description " << oneLine(lesson.description) << "\n";
    for (const LessonStep& step : lesson.steps){
        out << "\nstep " << lessonStepTypeName(step.type) << "\n";
        if (!step.title.empty()) out << "title " << oneLine(step.title) << "\n";
        for (const std::string& paragraph : step.paragraphs){
            // A paragraph typed with line breaks is saved as several paragraphs, one per line
            std::istringstream lines(paragraph);
            std::string line;
            while (std::getline(lines, line)) if (!line.empty()) out << "text " << line << "\n";
        }
        if (!step.file.empty()) out << "file " << oneLine(step.file) << "\n";
        if (!step.caption.empty()) out << "caption " << oneLine(step.caption) << "\n";
        if (!step.exercise.empty()) out << "exercise " << oneLine(step.exercise) << "\n";
        if (step.goal > 0) out << "goal " << step.goal << (step.type == LessonStepType::Play ? "%" : "") << "\n";
    }
    std::error_code ec;
    fs::create_directories(folder, ec);
    return writeFileAtomically((fs::path(folder) / LESSON_FILE_NAME).string(), out.str(), error);
}

std::vector<LessonEntry> scanLessons(const std::string& dir, bool builtIn){
    std::vector<LessonEntry> lessons;
    std::error_code ec; // a missing folder just means no lessons
    for (const fs::directory_entry& folder : fs::directory_iterator(dir, ec)){
        if (!folder.is_directory() || !fs::exists(folder.path() / LESSON_FILE_NAME)) continue;
        LessonEntry entry;
        entry.folder = folder.path().string();
        entry.builtIn = builtIn;
        std::string name = folder.path().filename().string();
        entry.id = std::string(builtIn ? "builtin-" : "user-") + name;
        if (!loadLesson(entry.folder, entry.lesson, entry.error)){
            entry.lesson.title = name;
            // Menus show the error: "folder/lesson.lesson" is enough there, the full path would take several lines
            std::string path = (folder.path() / LESSON_FILE_NAME).string();
            if (entry.error.rfind(path, 0) == 0) entry.error = name + "/" + LESSON_FILE_NAME + entry.error.substr(path.size());
        }
        lessons.push_back(entry);
    }
    std::sort(lessons.begin(), lessons.end(), [](const LessonEntry& a, const LessonEntry& b){
        if (a.lesson.category != b.lesson.category) return a.lesson.category < b.lesson.category;
        return a.lesson.title < b.lesson.title;
    });
    return lessons;
}

void checkLessonExercises(std::vector<LessonEntry>& lessons, const std::vector<ExerciseEntry>& exercises){
    for (LessonEntry& entry : lessons){
        if (!entry.error.empty()) continue;
        std::string where = fs::path(entry.folder).filename().string() + "/" + LESSON_FILE_NAME + ": step ";
        for (size_t i = 0; i < entry.lesson.steps.size() && entry.error.empty(); i++){
            const LessonStep& step = entry.lesson.steps[i];
            if (step.type != LessonStepType::Exercise) continue;
            const ExerciseEntry* found = findExercise(exercises, entry.builtIn, step.exercise);
            std::string stepName = where + std::to_string(i + 1) + " (" + step.exercise + ")";
            if (!found) entry.error = stepName + ": there's no " + step.exercise + ".exercise";
            else if (!found->error.empty()) entry.error = stepName + ": that exercise has an error of its own";
            else if (found->exercise.type == ExerciseType::Routine) entry.error = stepName + ": a routine can't be a lesson step";
        }
    }
}

bool stepPassed(const LessonProgress& progress, int step){
    return std::binary_search(progress.passed.begin(), progress.passed.end(), step);
}

void passStep(LessonProgress& progress, int step){
    auto at = std::lower_bound(progress.passed.begin(), progress.passed.end(), step);
    if (at == progress.passed.end() || *at != step) progress.passed.insert(at, step);
}

LessonProgress loadLessonProgress(const std::string& path){
    LessonProgress progress;
    std::ifstream file(path);
    std::string line;
    while (std::getline(file, line)){
        std::istringstream ss(line);
        std::string key;
        if (!(ss >> key) || key[0] == '#') continue;
        int value;
        if (key == "reached" && ss >> value && value >= 0) progress.reached = value;
        else if (key == "completed" && ss >> value) progress.completed = value == 1;
        else if (key == "passed") while (ss >> value) if (value >= 0) passStep(progress, value);
    }
    return progress;
}

bool saveLessonProgress(const std::string& path, const LessonProgress& progress, std::string& error){
    std::ostringstream out;
    out << "# hardthz progress: lesson\n";
    out << "version 1\n";
    out << "reached " << progress.reached << "\n";
    out << "passed";
    for (int step : progress.passed) out << " " << step;
    out << "\n";
    out << "completed " << (progress.completed ? 1 : 0) << "\n";
    return writeFileAtomically(path, out.str(), error);
}
