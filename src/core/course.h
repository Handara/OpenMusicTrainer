#pragma once

#include "core/lessondoc.h"

#include <map>
#include <string>
#include <vector>

// Courses: many small steps, from the very start (never having held an instrument) a little at a time. A course is
// levels (its units), a level is chapters (its lessons), a chapter is a few words to read and drills (its exercises,
// written in place or named), each drill scored by its best run; a chapter opens once the one before is passed. One
// text file.
//
//   # lahn course
//   version 1
//   title Reading music
//   description From your first note on the staff to reading melodies.
//   instrument guitar               (guitar, bass or piano: the exercises written in place are played on it)
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
//   drill E and F, no hints         (an exercise written in place, with its own name)
//   type notes
//   notes E4 F4
//
// Lesson titles are unique within a course: a lesson's progress is kept by its title.
//
// Version 2 (what the course tools write now, and the lesson maker): levels of chapters, each chapter a lesson's pages
// (core/lessondoc), written under its own line; its drills are its exercise blocks, each kept by its number ('id').
//
//   # lahn course
//   version 2
//   title Reading music
//   instrument guitar
//
//   level The high E string
//
//   chapter Your first note
//   page
//     section wide-narrow
//       block text
//         text Music is written on five lines, the staff.
//     column
//       block staff
//         notes E4
//     section
//       block exercise E alone
//         id 2
//         type notes
//         notes E4
//         show staff
//
// A version 1 course reads as version 2: each chapter a page, its words then its drills, each drill numbered by its
// step (as its progress was kept).

struct CourseUnit {
    std::string title;
    int firstLesson = 0; // into Course::lessons
    int lessonCount = 0;
};

struct CourseLesson {
    Lesson lesson;  // its title (and, read from a version 1 course, its steps)
    LessonDoc doc;  // the chapter: its pages
    int unit = 0;
    std::string id; // from its title: "your-first-note"
};


struct Course {
    std::string title;
    std::string description;
    ExerciseInstrument instrument = ExerciseInstrument::Guitar;
    std::vector<CourseUnit> units;
    std::vector<CourseLesson> lessons;
};

bool parseCourse(const std::string& text, const std::string& path, Course& out, std::string& error);
// A version 1 chapter as a page: its words, then its drills, each numbered by its step
LessonDoc chapterFromSteps(const Lesson& lesson, ExerciseInstrument instrument);
// The course as version 2 text
std::string writeCourse(const Course& course);
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

// A chapter's drills: its exercise blocks, with what passes each (the course keeps each by its number)
struct CourseDrill {
    int lesson = 0;          // its chapter
    BlockPlace place;        // its block in the chapter
    std::string id;          // "<chapter id>-<step from 1>": its score and its own progress are kept by it
    std::string name;        // for the list: its own, or the chapter's with its number
    int passPercent = 100;   // the score a run needs
};
std::vector<CourseDrill> courseDrills(const Course& course, int lesson);
// What a run of an exercise must score to pass it (percent): its own rule (so many right of so many, a pass's share)
int exercisePassPercent(const ExerciseFile& exercise);

// Each drill's best score, kept in one file per course; a chapter with no drills is passed once it's been read
struct CourseScores {
    std::map<std::string, int> best; // drill id (or "<chapter id>-read") -> percent
};
CourseScores loadCourseScores(const std::string& path);
bool saveCourseScores(const std::string& path, const CourseScores& scores, std::string& error);
// A run's score, kept if it's the best: true if it is
bool recordCourseScore(CourseScores& scores, const std::string& drill, int percent);

struct ChapterState {
    int percent = 0;     // its drills' best scores, on average
    bool passed = false; // every drill passed (or, without drills, read)
    bool perfect = false;// every drill at 100%
    int stars = 0, starsPossible = 0; // its drills' stars (drillStars), of three each
};
// A drill's stars by its best: one passed, two at 95% or more, three every note right; none not passed yet
int drillStars(int best, int passPercent);
ChapterState chapterState(const Course& course, int lesson, const CourseScores& scores);
// The first, one whose chapter before is passed, or one played in already
bool chapterOpen(const Course& course, int lesson, const CourseScores& scores);
int levelPercent(const Course& course, int unit, const CourseScores& scores);
int coursePercent(const Course& course, const CourseScores& scores);
int courseContinue(const Course& course, const CourseScores& scores); // the first chapter not passed (the last if all are)
