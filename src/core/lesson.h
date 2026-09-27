#pragma once

#include "core/exercisefile.h"

#include <string>
#include <vector>

// Lessons: steps to go through in order (text, pictures, sound, video, exercises, songs to play), written as a
// text file anyone can make and share. A lesson is a folder, like a song: lesson.lesson and its media next to it.
//
//   # OpenMusicTrainer lesson
//   version 1
//   title Your first chords
//   category Guitar basics
//   author Someone
//   description Two chords and your first strum.
//
//   step text
//   title What is a chord?        (any step can have a title)
//   text Three or more notes played together.
//   text Each text line is a paragraph.
//
//   step image                    (and the same for audio and video)
//   file em-shape.png             (a file in the lesson's folder)
//   caption E minor, fingers 2 and 3 on the 2nd fret
//
//   step exercise
//   exercise e-minor-open         (an exercise's file name, without .exercise)
//   goal 2                        (optional: see below)
//
//   step play
//   file riff.chart               (a chart in the lesson's folder, with its audio)
//   goal 90                       (optional)
//
// Exercise and play steps must be passed to go on. A goal counts clean passes for a scale drill (default 1), right
// answers in a row for an interval exercise (default 5), and the percentage of notes hit for a play step (80).

enum class LessonStepType { Text, Image, Audio, Video, Exercise, Play };

struct LessonStep {
    LessonStepType type = LessonStepType::Text;
    std::string title;                   // optional heading
    std::vector<std::string> paragraphs; // text steps
    std::string file;                    // image, audio, video and play steps: a file in the lesson's folder
    std::string caption;                 // image, audio and video steps, optional
    std::string exercise;                // exercise steps: an exercise's file name
    int goal = 0;                        // exercise and play steps: 0 = the usual goal (lessonGoal)
};

struct Lesson {
    std::string title;
    std::string category = "Other";
    std::string author;
    std::string description;
    std::vector<LessonStep> steps;
};

const char* const LESSON_FILE_NAME = "lesson.lesson";

const char* lessonStepTypeName(LessonStepType type); // "text", "image"... as written in the file
// The file types each step accepts (lower case, with the dot), empty for steps without a file
const std::vector<std::string>& lessonFileExtensions(LessonStepType type);

// Strict, like charts and exercises: lessons are shared. Media files must exist in the lesson's folder, and a
// play step's chart must load.
bool loadLesson(const std::string& folder, Lesson& out, std::string& error);
bool saveLesson(const std::string& folder, const Lesson& lesson, std::string& error);

// The goal a step really has: its own, or the usual one for what it runs
int lessonGoal(const LessonStep& step, ExerciseType exerciseType = ExerciseType::Intervals);

struct LessonEntry {
    std::string folder;
    std::string id;     // names its progress file: "builtin-<folder name>" or "user-<folder name>"
    bool builtIn;
    Lesson lesson;      // if it failed to load, only the title (the folder name) is set
    std::string error;  // why it can't be played, empty if it can
};

// Every folder with a lesson.lesson in it, sorted by category then title
std::vector<LessonEntry> scanLessons(const std::string& dir, bool builtIn);
// Exercise steps name exercises: marks lessons whose exercise is missing, broken, or a routine (a routine has no
// goal a lesson could check)
void checkLessonExercises(std::vector<LessonEntry>& lessons, const std::vector<ExerciseEntry>& exercises);
