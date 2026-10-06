#pragma once

#include "core/chords.h"
#include "core/drill.h"
#include "core/necktrainer.h"
#include "core/notequiz.h"
#include "core/fretboard.h"
#include "core/reading.h"
#include "core/rhythm.h"
#include "core/singing.h"
#include "core/intervals.h"
#include "core/routine.h"

#include <string>
#include <vector>

// Learn-mode exercises as files anyone can write and share. The *type* of exercise is built into the game
// (how it plays and judges, like the interval quiz); an exercise file picks a type and sets its rules:
//
//   # lahn exercise
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
//
// Fretboard note finding (`type fretboard`) sets, all optional:
//   strings 1 2                  (which strings are asked, 1 = the lowest; default: all)
//   frets 0 12                   (the lowest and highest fret an answer can be on)
//   notes naturals               (naturals: C D E F G A B; all: the sharps and flats too)
//   tuning 40 45 50 55 59 64     (MIDI pitch per string, lowest first)
//
// A rhythm drill (`type rhythm`) reads a new rhythm each pass, all optional:
//   cells quarter eighths rest   (the beat-long figures it's built from: quarter, rest, eighths, offbeat, triplets,
//                                 sixteenths, gallop, reverse_gallop, dotted)
//   bars 2                       (1 to 8)
//   time 4                       (beats per bar, 2 to 7: x/4)
//   tempo 60 140 4               (start, goal, step, in bpm)
//   pass 90                      (percent right for a pass to count as clean and speed up)
//   tuning 40 45 50 55 59 64     (it's played on the open string written nearest the staff's middle line)
//
// A sight reading drill (`type reading`) reads a new melody each pass, all optional:
//   key C                        (the root: C, F#, Bb...)
//   scale major                  (see core/scales.cpp for every name)
//   frets 0 3                    (the position: the notes are found in these frets...)
//   strings 1 2 3                (...on these strings, 1 = the lowest; default: all)
//   leap 2                       (the widest move, in notes of the scale: 1 = by step only)
//   notes E4 F4                  (or just these notes, at random, each where it's lowest on the neck)
//   and a rhythm drill's cells, bars, time, tempo, pass and tuning
//
// A chord change drill (`type chords`), all optional:
//   chords Em C G D              (chord names, played in turn: C, Cmaj7, C7, D, Dm, D7, E, Em, E7, F, Fmaj7, G, G7,
//                                 A, Am, A7, Am7, B7)
//   beats 4                      (beats per chord, 1 to 8)
//   rounds 2                     (times through the chords in one pass, 1 to 8)
//   tempo 60 120 4               (start, goal, step, in bpm)
//   pass 80                      (percent of changes right for a pass to count as clean and speed up)
//
// Singing notes back (`type singing`), all optional:
//   range 48 67                  (the lowest and highest note asked, MIDI: C3 to G4)
//   notes naturals               (naturals: C D E F G A B; all: the sharps and flats too)
//   octave any                   (any: the right note in any octave counts; exact: in the octave played)
//   tolerance 30                 (how many cents off still counts as in tune, 5 to 50)
//   hold 1                       (seconds to hold it in tune, 0.3 to 3)
//
// Play this note (`type notes`, core/notequiz): a few notes asked one at a time, no clock, from a first time on the
// instrument to reading
//   notes E4 F4 G4               (by name: each where it's lowest on the neck, open strings first)
//   places 6:0 6:1               (or by place, string:fret, 1 = the lowest string)
//   strings 5 6                  (names placed only on these, 1 = the lowest)
//   show neck                    (neck: where to play it; name: the note's name; staff: the note written;
//                                 ear: the note heard, to play back)
//   where yes                    (the place on the neck shown too, for name, staff and ear: default no)
//   candidates yes               (every note that could be asked outlined on the neck: default yes by ear)
//   reference E4                 (by ear: a note played first, each time, to hear the other against)
//   octave exact                 (exact, or any: the name's enough; default exact, any for show name)
//   order random                 (random, or in_order: as written, going round)
//   count 8                      (notes in a run)
//   pass 7                       (right the first time, for the run to pass)
//   key C major                  (the staff's key signature)
//   instrument guitar            (guitar or bass)
//
// Neck walk (`type neck_walk`), a game (core/neckwalk), all optional:
//   tune neck-walk.groove        (its tune, core/groove: beside the exercise, or in the game's games folder)
//   level easy                   (the level it starts at: easy, normal or hard; the player changes it before playing)
//   instrument guitar            (guitar or bass)
//
// A routine (`type routine`) is a playlist of other exercises, a few minutes each, done one after the other:
//   step e-minor-open 3          (an exercise's file name without .exercise, then minutes: at least one step)
//   step intervals-up 5
// A built-in routine uses built-in exercises; the player's own routines look in their own exercises first.

enum class ExerciseType { Intervals, Scale, Routine, Fretboard, Rhythm, Reading, Chords, Singing, Neck, NeckWalk, Notes };

// What an exercise is played on: Learn lists it only for that instrument (Any: whichever)
enum class ExerciseInstrument { Any, Guitar, Bass };

struct ExerciseFile {
    ExerciseType type = ExerciseType::Intervals;
    // Its 'instrument' line, else by what it is: played on the instrument, a guitar's unless its tuning is a bass's
    // (its lowest string below C2); answered some other way (intervals, singing, rhythm), any
    ExerciseInstrument instrument = ExerciseInstrument::Any;
    std::string title;
    std::string category = "Other";
    std::string author;
    std::string description;
    IntervalConfig intervals; // the rules, for type Intervals
    ScaleDrillConfig drill;   // the rules, for type Scale
    FretboardConfig fretboard; // the rules, for type Fretboard
    RhythmConfig rhythm;       // the rules, for type Rhythm
    ReadingConfig reading;     // the rules, for type Reading
    ChordDrillConfig chords;   // the rules, for type Chords
    SingingConfig singing;     // the rules, for type Singing
    NeckRoutine neck;          // where it starts, for type Neck (the player changes it as they go)
    bool neckOnBass = false;   //   played on a bass, not a guitar (Neck and NeckWalk)
    std::string tune = "neck-walk.groove"; // a game's tune, for type NeckWalk
    int walkLevel = 0;                     //   the level it starts at (core/neckwalk)
    NoteQuizConfig noteQuiz;               // the notes asked, for type Notes (played on a bass: neckOnBass)
    KeySignature noteQuizKey;              //   the staff's key
    std::vector<RoutineStep> routine; // the steps, for type Routine
};

// Strict, like charts: exercises are shared, so authors get a clear error with its line number.
bool loadExerciseFile(const std::string& path, ExerciseFile& out, std::string& error);
// Whether it's played on the instrument (so it's checked in tune before): notes, the neck, reading, scales, chords...
bool exercisePlayedOnInstrument(const ExerciseFile& exercise);
// The same from text: `path` and `firstLine` say where it's from in errors. With `inlineTitle`, it's written inside
// another file (a course's lesson): no version line needed, and the title is that unless it has its own.
bool parseExercise(const std::string& text, const std::string& path, int firstLine, const std::string& inlineTitle,
                   ExerciseFile& out, std::string& error);

struct ExerciseEntry {
    std::string path;
    std::string name;   // the file name without ".exercise": how routines refer to it
    std::string id;     // names its progress file: "builtin-<file name>" or "user-<file name>"
    bool builtIn;
    ExerciseFile exercise; // if it failed to load, only the title (the file name) is set
    std::string error;     // why it failed, empty if it's playable; shown so authors see it
};

// Every *.exercise file in the folder, sorted by category then title
std::vector<ExerciseEntry> scanExercises(const std::string& dir, bool builtIn);

// The exercise a routine or lesson names, or nullptr if there's none. Something built in only uses built-in
// exercises (so it works the same for everyone); the player's own look in their own exercises first.
const ExerciseEntry* findExercise(const std::vector<ExerciseEntry>& entries, bool fromBuiltIn, const std::string& name);
// A routine can only be checked once every exercise is loaded: this marks, with an error, each routine that names
// a missing or broken exercise, or another routine (routines inside routines could loop forever)
void checkRoutines(std::vector<ExerciseEntry>& entries);
