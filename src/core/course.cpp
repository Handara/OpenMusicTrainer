#include "core/course.h"

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
        const bool playedOnOne = block.find("type notes") != std::string::npos || block.find("type neck") != std::string::npos;
        if (out.bass && playedOnOne && !saysInstrument) block += "instrument bass\n";
        std::string exerciseError;
        if (!parseExercise(block, path, blockLine, lesson()->title, step.inlineExercise, exerciseError)){
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
        const bool structural = key == "unit" || key == "lesson" || key == "title" || key == "text" || key == "exercise" || key == "goal";

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
                if (rest != "guitar" && rest != "bass") return lineError("instrument must be guitar or bass");
                out.bass = rest == "bass";
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
        if (key == "exercise"){
            LessonStep step;
            step.type = LessonStepType::Exercise;
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
