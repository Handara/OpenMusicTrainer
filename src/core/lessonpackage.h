#pragma once

#include <string>

// Lessons to share: a whole lesson in one file, a .lahnlesson file. It's a plain zip (any zip tool opens it) holding
// the lesson's folder's files side by side: lesson.lesson, and the pictures, sounds, videos and songs its blocks
// show. A course is one file already (a .course): it's shared as it is.

const char* const LESSON_PACKAGE_EXTENSION = ".lahnlesson";
const char* const COURSE_EXTENSION = ".course";

// Packs the lesson in `lessonFolder` into `packagePath` (replacing any file there only once the new one is complete).
// Only a lesson that plays goes (its file read strictly, its media there).
bool exportLessonPackage(const std::string& lessonFolder, const std::string& packagePath, std::string& error);

// Unpacks a package into a new folder of `lessonsDir`, named after the lesson's title (" (2)" and on if it's taken).
// Checked first: only plain file names, a lesson.lesson that plays. On failure nothing is left behind.
// `installedFolder` is the new lesson's folder.
bool installLessonPackage(const std::string& packagePath, const std::string& lessonsDir, std::string& installedFolder,
                          std::string& error);

// A course file copied into `coursesDir` once it reads (" (2)" and on if its name's taken there)
bool installCourseFile(const std::string& coursePath, const std::string& coursesDir, std::string& installedPath, std::string& error);
