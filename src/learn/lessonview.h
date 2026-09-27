#pragma once

#include "core/lesson.h"
#include "raylib.h"

#include <string>

// Draws one lesson step the way the student sees it: the lesson player shows it, and the lesson editor previews it
// with the very same code, so the preview can't differ from the real thing.

// What a step needs loaded to be shown: its picture and its sound. Kept between frames; a step with a different
// file reloads it. The sound plays through the song stream, so only one lesson clip plays at a time.
struct LessonMedia {
    std::string imagePath; // the loaded texture's file, empty if none
    Texture2D texture{};
    std::string audioPath; // the clip loaded into the song stream, empty if none
    std::string error;     // why the step's media couldn't be loaded
};

// Draws a step in the current ImGui window, `width` wide: its title, then what its type shows. `exercise` is the
// exercise an exercise step names (nullptr if it isn't found), for its title and goal.
void drawLessonStep(const LessonStep& step, const std::string& folder, const ExerciseEntry* exercise, LessonMedia& media, float width);
// Frees the texture and stops the clip: when the step changes, and when leaving
void releaseLessonMedia(LessonMedia& media);

// "Goal: 2 clean passes": what passing an exercise or play step takes
std::string lessonGoalText(const LessonStep& step, const ExerciseEntry* exercise);
