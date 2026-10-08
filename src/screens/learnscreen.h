#pragma once

#include "core/settings.h"

#include <memory>
#include <string>

class Exercise;
struct LessonEntry;

// Learn mode: lessons, and the exercises (built-in and the player's own .exercise files), and the one being practiced.

struct LearnSetup {
    std::string builtInExercises; // shipped with the game, read-only
    std::string userExercises;    // the player's own and downloaded ones
    std::string builtInLessons;   // lessons: one folder each, built-in and the player's own
    std::string userLessons;
    std::string progress;         // one progress file per exercise
    Settings settings;            // input device, offsets, note view... as they are when learn mode opens
    InputRole instrument = InputRole::Guitar; // what's played: only its courses and exercises are listed
    bool piano = false;                       //   a piano instead
};

void openLearnScreen(const LearnSetup& setup); // scans both exercise folders
void learnScreen();  // draws the menu or the running exercise
bool learnWantsEditor(); // the mode switch beside the title chose EDIT: the lesson editor, this frame
bool learnBack();    // Esc: ends the running exercise, or (from the menu) returns true to leave learn mode
// The instrument switch moved this frame (true once): the settings keep it as the one played now (a guitar or a
// bass), or the piano
bool learnChangedInstrument(InputRole& instrument, bool& piano);
// Something played on the instrument waits to start: it's checked in tune first, as before a song (true once). Then
// learnTuningDone says whether to go on (tuned, skipped, or no check needed) or not (the check was left).
bool learnWantsTuning(InputRole& instrument);
// Learn shows something to choose from (its lists, a chapter's page), not an exercise being played
bool learnInMenus();
void learnTuningDone(bool go);
void closeLearnScreen();
// The first course for the instrument played, opened (its levels): the welcome's way in for someone new
void learnOpenFirstCourse();
// The lesson maker's tryout: a lesson being made, played from one of its pages, with Learn's exercises (as last
// scanned); its progress kept in `progressPath`, apart from the student's
std::unique_ptr<Exercise> learnTryLesson(const LessonEntry& entry, int page, const std::string& progressPath);
