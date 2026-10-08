#pragma once

#include <string>

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
};

void openLessonEditor(const LessonEditorSetup& setup);

enum class LessonEditorChoice { None, Back };
LessonEditorChoice lessonEditorScreen(); // full-screen ImGui; handles its own Esc so it can warn about unsaved changes
void closeLessonEditor();                // safe to call more than once
