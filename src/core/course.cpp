#include "core/course.h"

#include "core/files.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

const int SUPPORTED_COURSE_VERSION = 1;

std::string courseSlug(const std::string& title){
    std::string slug;
    for (char c : title){
        if (std::isalnum((unsigned char)c)) slug += (char)std::tolower((unsigned char)c);
        else if (!slug.empty() && slug.back() != '-') slug += '-';
    }
    while (!slug.empty() && slug.back() == '-') slug.pop_back();
    return slug;
}

bool parseCourse(const std::string& source, const std::string& path, Course& out, std::string& error){
    out = Course{};
    int lineNumber = 0, version = 0;
    auto lineError = [&](const std::string& message){
        error = path + ":" + std::to_string(lineNumber) + ": " + message;
        return false;
    };
    // An exercise written in place: its lines, kept until the next step, then read as an exercise file
    std::string block;
    int blockLine = 0;
    bool inBlock = false, lastWasText = false;
    auto lesson = [&]() -> Lesson* { return out.lessons.empty() ? nullptr : &out.lessons.back().lesson; };
    auto flushBlock = [&]() -> bool {
        if (!inBlock) return true;
        inBlock = false;
        LessonStep& step = lesson()->steps.back();
        // The course's instrument, unless the exercise says (for the kinds that are played on one)
        const bool saysInstrument = block.find("\ninstrument ") != std::string::npos || block.rfind("instrument ", 0) == 0;
        const bool playedOnOne = block.find("type notes") != std::string::npos || block.find("type neck") != std::string::npos
                                 || (out.instrument == ExerciseInstrument::Piano && block.find("type reading") != std::string::npos);
        if (out.instrument != ExerciseInstrument::Guitar && playedOnOne && !saysInstrument)
            block += out.instrument == ExerciseInstrument::Bass ? "instrument bass\n" : "instrument piano\n";
        std::string exerciseError;
        if (!parseExercise(block, path, blockLine, step.title.empty() ? lesson()->title : step.title, step.inlineExercise, exerciseError)){
            error = exerciseError;
            return false;
        }
        if (step.inlineExercise.type == ExerciseType::Routine){
            error = path + ":" + std::to_string(blockLine) + ": a routine can't be a lesson's step (it has no goal to reach)";
            return false;
        }
        return true;
    };

    std::istringstream lines(source);
    std::string text;
    while (std::getline(lines, text)){
        lineNumber++;
        if (!text.empty() && text.back() == '\r') text.pop_back();
        std::istringstream ss(text);
        std::string key;
        if (!(ss >> key) || key[0] == '#'){
            if (inBlock) block += "\n"; // the exercise's own line numbers stay right
            continue;
        }
        std::string rest;
        std::getline(ss >> std::ws, rest);
        while (!rest.empty() && std::isspace((unsigned char)rest.back())) rest.pop_back();
        const bool structural = key == "unit" || key == "lesson" || key == "title" || key == "text" || key == "exercise" || key == "drill"
                                || key == "goal";

        if (inBlock && !structural){
            block += text + "\n";
            continue;
        }
        if (key != "goal" && !flushBlock()) return false;

        if (key == "version" && out.units.empty()){
            std::istringstream number(rest);
            if (!(number >> version)) return lineError("expected: version <number>");
            continue;
        }
        if (out.units.empty() && key != "unit"){
            // The header
            if (key == "title") out.title = rest;
            else if (key == "description") out.description = rest;
            else if (key == "instrument"){
                if (rest == "guitar") out.instrument = ExerciseInstrument::Guitar;
                else if (rest == "bass") out.instrument = ExerciseInstrument::Bass;
                else if (rest == "piano") out.instrument = ExerciseInstrument::Piano;
                else return lineError("instrument must be guitar, bass or piano");
            } else if (key == "author"){
            } else return lineError("unknown setting '" + key + "' before the first unit");
            continue;
        }
        if (key == "unit"){
            if (rest.empty()) return lineError("a unit needs a title");
            out.units.push_back({ rest, (int)out.lessons.size(), 0 });
            lastWasText = false;
            continue;
        }
        if (key == "lesson"){
            if (rest.empty()) return lineError("a lesson needs a title");
            CourseLesson entry;
            entry.lesson.title = rest;
            entry.unit = (int)out.units.size() - 1;
            entry.id = courseSlug(rest);
            for (const CourseLesson& other : out.lessons)
                if (other.id == entry.id) return lineError("another lesson is called '" + rest + "' already: lessons need their own titles");
            out.lessons.push_back(entry);
            out.units.back().lessonCount++;
            lastWasText = false;
            continue;
        }
        if (!lesson()) return lineError("'" + key + "' before the unit's first lesson");
        std::vector<LessonStep>& steps = lesson()->steps;
        if (key == "title" || key == "text"){
            if (rest.empty()) return lineError("'" + key + "' needs some text");
            // A heading starts a text step; text goes on the one just before, or starts one
            if (key == "title" || !lastWasText){
                steps.push_back(LessonStep{});
                steps.back().type = LessonStepType::Text;
            }
            if (key == "title") steps.back().title = rest;
            else steps.back().paragraphs.push_back(rest);
            lastWasText = true;
            continue;
        }
        lastWasText = false;
        if (key == "exercise" || key == "drill"){
            LessonStep step;
            step.type = LessonStepType::Exercise;
            if (key == "drill"){ // written in place, named
                if (rest.empty()) return lineError("a drill needs a name");
                step.title = rest;
                rest.clear();
            }
            if (rest.empty()){
                step.inlined = true;
                inBlock = true;
                block.clear();
                blockLine = lineNumber + 1;
            } else {
                step.exercise = rest;
            }
            steps.push_back(step);
            continue;
        }
        if (key == "goal"){
            if (steps.empty() || steps.back().type != LessonStepType::Exercise) return lineError("'goal' goes with an exercise");
            std::istringstream number(rest);
            if (!(number >> steps.back().goal) || steps.back().goal < 1) return lineError("goal is a count, 1 or more");
            continue;
        }
        return lineError("unknown setting '" + key + "' (in a lesson: title, text, exercise, goal)");
    }
    lineNumber++;
    if (!flushBlock()) return false;

    if (version == 0){ error = path + ": missing 'version'"; return false; }
    if (version > SUPPORTED_COURSE_VERSION){
        error = path + ": course format v" + std::to_string(version) + " is newer than this build supports (v"
              + std::to_string(SUPPORTED_COURSE_VERSION) + ")";
        return false;
    }
    if (out.title.empty()){ error = path + ": missing 'title'"; return false; }
    if (out.lessons.empty()){ error = path + ": a course needs at least one lesson"; return false; }
    for (const CourseLesson& entry : out.lessons)
        if (entry.lesson.steps.empty()){ error = path + ": the lesson '" + entry.lesson.title + "' has no steps"; return false; }
    for (const CourseUnit& unit : out.units)
        if (unit.lessonCount == 0){ error = path + ": the unit '" + unit.title + "' has no lessons"; return false; }
    return true;
}

bool loadCourse(const std::string& path, Course& out, std::string& error){
    std::ifstream file(path);
    if (!file){
        error = path + ": could not open file";
        return false;
    }
    std::stringstream text;
    text << file.rdbuf();
    return parseCourse(text.str(), path, out, error);
}

std::vector<CourseEntry> scanCourses(const std::string& dir){
    std::vector<CourseEntry> entries;
    std::error_code ec; // no folder: no courses
    for (const fs::directory_entry& file : fs::directory_iterator(dir, ec)){
        if (!file.is_regular_file() || file.path().extension() != ".course") continue;
        CourseEntry entry;
        entry.path = file.path().string();
        entry.id = "course-" + file.path().stem().string();
        if (!loadCourse(entry.path, entry.course, entry.error)) entry.course.title = file.path().stem().string();
        entries.push_back(entry);
    }
    std::sort(entries.begin(), entries.end(), [](const CourseEntry& a, const CourseEntry& b){ return a.path < b.path; });
    return entries;
}

int exercisePassPercent(const ExerciseFile& exercise){
    switch (exercise.type){
        case ExerciseType::Notes: {
            const NoteQuizConfig& quiz = exercise.noteQuiz;
            return quiz.count > 0 ? (std::min(quiz.pass, quiz.count) * 100 + quiz.count - 1) / quiz.count : 100; // rounded up
        }
        case ExerciseType::Reading: return exercise.reading.tempo.passPercent;
        case ExerciseType::Scale: return exercise.drill.tempo.passPercent;
        case ExerciseType::Rhythm: return exercise.rhythm.tempo.passPercent;
        case ExerciseType::Chords: return exercise.chords.tempo.passPercent;
        default: return 100;
    }
}

std::vector<CourseDrill> courseDrills(const Course& course, int lesson){
    std::vector<CourseDrill> drills;
    if (lesson < 0 || lesson >= (int)course.lessons.size()) return drills;
    const CourseLesson& chapter = course.lessons[lesson];
    for (int i = 0; i < (int)chapter.lesson.steps.size(); i++){
        const LessonStep& step = chapter.lesson.steps[i];
        if (step.type != LessonStepType::Exercise) continue;
        CourseDrill drill;
        drill.lesson = lesson;
        drill.step = i;
        drill.id = chapter.id + "-" + std::to_string(i + 1);
        drill.name = !step.title.empty() ? step.title : step.inlined ? "" : step.exercise;
        drill.passPercent = step.inlined ? exercisePassPercent(step.inlineExercise) : 100;
        drills.push_back(drill);
    }
    for (size_t i = 0; i < drills.size(); i++)
        if (drills[i].name.empty()) drills[i].name = drills.size() == 1 ? chapter.lesson.title : chapter.lesson.title + " " + std::to_string(i + 1);
    return drills;
}

CourseScores loadCourseScores(const std::string& path){
    CourseScores scores;
    std::ifstream in(path);
    std::string drill;
    int percent;
    while (in >> drill >> percent) scores.best[drill] = std::clamp(percent, 0, 100);
    return scores;
}

bool saveCourseScores(const std::string& path, const CourseScores& scores, std::string& error){
    std::ostringstream out;
    for (const auto& [drill, percent] : scores.best) out << drill << " " << percent << "\n";
    return writeFileAtomically(path, out.str(), error);
}

bool recordCourseScore(CourseScores& scores, const std::string& drill, int percent){
    percent = std::clamp(percent, 0, 100);
    auto found = scores.best.find(drill);
    if (found != scores.best.end() && found->second >= percent) return false;
    scores.best[drill] = percent;
    return true;
}

static int bestOf(const CourseScores& scores, const std::string& drill){
    auto found = scores.best.find(drill);
    return found == scores.best.end() ? 0 : found->second;
}

ChapterState chapterState(const Course& course, int lesson, const CourseScores& scores){
    ChapterState state;
    const std::vector<CourseDrill> drills = courseDrills(course, lesson);
    if (drills.empty()){
        const bool read = lesson >= 0 && lesson < (int)course.lessons.size() && bestOf(scores, course.lessons[lesson].id + "-read") > 0;
        state.percent = read ? 100 : 0;
        state.passed = state.perfect = read;
        return state;
    }
    int sum = 0;
    state.passed = state.perfect = true;
    for (const CourseDrill& drill : drills){
        const int best = bestOf(scores, drill.id);
        sum += best;
        if (best < drill.passPercent) state.passed = false;
        if (best < 100) state.perfect = false;
    }
    state.percent = sum / (int)drills.size();
    return state;
}

bool chapterOpen(const Course& course, int lesson, const CourseScores& scores){
    if (lesson <= 0 || chapterState(course, lesson - 1, scores).passed) return true;
    // Once played in, it stays open (a drill added to a chapter before it later doesn't close it again)
    for (const CourseDrill& drill : courseDrills(course, lesson)) if (scores.best.count(drill.id)) return true;
    return lesson < (int)course.lessons.size() && scores.best.count(course.lessons[lesson].id + "-read") > 0;
}

int levelPercent(const Course& course, int unit, const CourseScores& scores){
    if (unit < 0 || unit >= (int)course.units.size() || course.units[unit].lessonCount == 0) return 0;
    int sum = 0;
    for (int i = course.units[unit].firstLesson; i < course.units[unit].firstLesson + course.units[unit].lessonCount; i++)
        sum += chapterState(course, i, scores).percent;
    return sum / course.units[unit].lessonCount;
}

int coursePercent(const Course& course, const CourseScores& scores){
    if (course.lessons.empty()) return 0;
    int sum = 0;
    for (int i = 0; i < (int)course.lessons.size(); i++) sum += chapterState(course, i, scores).percent;
    return sum / (int)course.lessons.size();
}

int courseContinue(const Course& course, const CourseScores& scores){
    for (int i = 0; i < (int)course.lessons.size(); i++) if (!chapterState(course, i, scores).passed) return i;
    return std::max(0, (int)course.lessons.size() - 1);
}
