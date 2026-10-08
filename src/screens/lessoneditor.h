#pragma once

#include "core/settings.h"

#include <string>
#include <vector>

// The lesson maker: pick a lesson (or make a new one), then make it: its pages in an outline at the left with the
// blocks to add, the page in the middle drawn exactly as the student will see it (a click chooses what's under it),
// and what's chosen's settings at the right. Changes are undone and redone; the page shown can be tried out as it'd be
// played. Built-in lessons are read-only: saving one creates an editable copy (media included) in the player's
// lessons folder.

struct LessonEditorSetup {
    std::string builtInLessons;
    std::string userLessons;
    std::string builtInExercises; // exercise steps name exercises: the editor lists them to pick from
    std::string userExercises;
    std::vector<std::string> songFolders; // the game's songs, for song blocks to play
    std::string builtInCourses;           // courses: the game's, and the player's own (made, or built-in ones changed)
    std::string userCourses;
    Settings settings;                    // the player's input: notes are recorded from it
};

void openLessonEditor(const LessonEditorSetup& setup);

enum class LessonEditorChoice { None, Back };
LessonEditorChoice lessonEditorScreen(); // full-screen ImGui; handles its own Esc so it can warn about unsaved changes
void closeLessonEditor();                // safe to call more than once
