#pragma once

#include "core/intervals.h"

#include <string>
#include <vector>

// Learn-mode exercises as files anyone can write and share. The *type* of exercise is built into the game
// (how it plays and judges, like the interval quiz); an exercise file picks a type and sets its rules:
//
//   # OpenMusicTrainer exercise
//   version 1
//   type intervals
//   title Major or minor 3rd?
//   category Ear training
//   author Someone
//   description Two notes going up. Is it a major or a minor 3rd?
//   direction up                 (up, down or together)
//   intervals 4 3                (semitones, in the order they unlock)
//   start 2                      (how many are unlocked at first)
//   unlock 9 10                  (right answers needed, out of the last how many)
//   range 48 67                  (lowest and highest starting note, MIDI)
//   gap 0.7                      (seconds between the two notes)
//
// Everything after `description` is optional and defaults to the classic full interval course.

enum class ExerciseType { Intervals };

struct ExerciseFile {
    ExerciseType type = ExerciseType::Intervals;
    std::string title;
    std::string category = "Other";
    std::string author;
    std::string description;
    IntervalConfig intervals; // the rules, for type Intervals
};

// Strict, like charts: exercises are shared, so authors get a clear error with its line number.
bool loadExerciseFile(const std::string& path, ExerciseFile& out, std::string& error);

struct ExerciseEntry {
    std::string path;
    std::string id;     // names its progress file: "builtin-<file name>" or "user-<file name>"
    bool builtIn;
    ExerciseFile exercise; // if it failed to load, only the title (the file name) is set
    std::string error;     // why it failed, empty if it's playable; shown so authors see it
};

// Every *.exercise file in the folder, sorted by category then title
std::vector<ExerciseEntry> scanExercises(const std::string& dir, bool builtIn);
