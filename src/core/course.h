#pragma once

#include "core/lesson.h"

#include <string>
#include <vector>

// Courses: a path through many small lessons, Duolingo-style, from the very start (never having held an instrument)
// a little at a time; each lesson opens once the one before is done. One text file, units of lessons, each lesson a
// few steps: text to read, and exercises, written in place or named.
//
//   # lahn course
//   version 1
//   title Reading music
//   description From your first note on the staff to reading melodies.
//   instrument guitar               (guitar or bass: the exercises written in place are played on it)
//
//   unit The high E string
//
//   lesson Your first note
//   title The staff                 (a text step with a heading; text lines after it are its paragraphs)
//   text Music is written on five lines, the staff.
//   exercise                        (an exercise written in place: its settings follow, as in an .exercise file,
//   type notes                       no version or title needed)
//   notes E4
//   show staff
//   goal 2                          (optional: the step's goal, as in lessons)
//
//   lesson E and F
//   exercise reading-first-notes    (or an exercise by its file name)
//
// Lesson titles are unique within a course: a lesson's progress is kept by its title.

struct CourseUnit {
    std::string title;
    int firstLesson = 0; // into Course::lessons
    int lessonCount = 0;
};

struct CourseLesson {
    Lesson lesson;
    int unit = 0;
    std::string id; // from its title: "your-first-note"
};

struct Course {
    std::string title;
    std::string description;
    bool bass = false;
    std::vector<CourseUnit> units;
    std::vector<CourseLesson> lessons;
};

bool parseCourse(const std::string& text, const std::string& path, Course& out, std::string& error);
bool loadCourse(const std::string& path, Course& out, std::string& error);

struct CourseEntry {
    std::string path;
    std::string id;    // names its progress files: "course-<file name>"
    Course course;
    std::string error; // why it can't be played, empty if it can
};
// Every .course in a folder, sorted by file name (numbered: 01-first-steps.course comes first)
std::vector<CourseEntry> scanCourses(const std::string& dir);

// "Your first note" -> "your-first-note"
std::string courseSlug(const std::string& title);
