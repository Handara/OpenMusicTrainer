#pragma once

#include "core/drill.h"
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
//
// A scale drill (`type scale`) instead sets, all optional:
//   key G                        (the root: C, F#, Bb...)
//   scale major                  (see core/scales.cpp for every name)
//   octaves 2
//   fingering position           (position or 3nps: three notes per string)
//   position 2                   (index finger's fret; default: one below the root on the lowest string)
//   direction up_down            (up, down or up_down)
//   notes_per_beat 2             (1 to 4)
//   tempo 60 160 4               (start, goal, step, in bpm)
//   pass 90                      (percent right for a pass to count as clean and speed up)
//   tuning 40 45 50 55 59 64     (MIDI pitch per string, lowest first)

enum class ExerciseType { Intervals, Scale };

struct ExerciseFile {
    ExerciseType type = ExerciseType::Intervals;
    std::string title;
    std::string category = "Other";
    std::string author;
    std::string description;
    IntervalConfig intervals; // the rules, for type Intervals
    ScaleDrillConfig drill;   // the rules, for type Scale
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
