#pragma once

#include <string>

// The lesson editor: pick a lesson (or make a new one), then edit its details and steps, with a live preview of the
// selected step drawn exactly as the student will see it. Built-in lessons are read-only: saving one creates an
// editable copy (media included) in the player's lessons folder.

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
