#pragma once

#include "core/necktrainer.h"

#include <random>
#include <string>
#include <vector>

// "Play this note": a few notes asked one at a time, with no clock: the player plays each when ready and gets the
// next. Small enough for a first time on an instrument (the place shown on the neck: "the open high E string"), then
// for learning the notes' names, for reading (the note on the staff), and for the ear (the note heard, played back).
// Pure logic: which notes, in what order, and how the player did; the screen shows them (learn/notequizexercise).

// What's given: where it's played, its name, the note written, or the note heard
enum class NotePrompt { Neck, Name, Staff, Ear };

struct NoteQuizConfig {
    std::vector<int> tuning = { 40, 45, 50, 55, 59, 64 };
    bool piano = false;          // played on a piano's keys: one "string" tuned to 0, each note's fret its pitch
    std::vector<NeckStep> notes; // the notes asked, each where it's played (string 0 the lowest)
    NotePrompt prompt = NotePrompt::Neck;
    bool showWhere = false;      // the place on the neck shown too (a name or a note on the staff): to learn it
    bool candidates = false;     // every note that could be asked outlined on the neck (by ear: which one is it?)
    int reference = -1;          // by ear: a note played before each one (the key's root), -1 for none
    bool anyOctave = false;      // the note's name is enough, in any octave
    bool inOrder = false;        // asked in the order written, going round; otherwise at random
    int count = 8;               // notes asked in a run
    int pass = 7;                // right the first time, for the run to pass
};

struct NoteQuizRun {
    std::vector<NeckStep> prompts;
    size_t next = 0;             // the prompt now
    std::vector<bool> firstTime; // each prompt: right on the first try
    bool slipped = false;        // a wrong note on the prompt now
    int mistakes = 0;
    int lastWrong = -1;          // the latest wrong note (MIDI), for showing it
};

// `count` notes from the config's: in order (going round), or at random (no pattern to follow: the same note up to
// three times running, every note at least once when there's room)
std::vector<NeckStep> noteQuizPrompts(const NoteQuizConfig& config, std::mt19937& random);
void startNoteQuiz(NoteQuizRun& run, const std::vector<NeckStep>& prompts);
// A note heard: true if it's the one asked (then the next is), false for another (it stays)
bool playNoteQuiz(NoteQuizRun& run, const NoteQuizConfig& config, int pitch);
bool noteQuizDone(const NoteQuizRun& run);
int noteQuizRight(const NoteQuizRun& run); // right the first time
bool noteQuizPassed(const NoteQuizRun& run, const NoteQuizConfig& config);

// Notes by name ("E4", "F4"): each where it's lowest on the neck, the open strings and first frets first, on the
// `strings` allowed (0 the lowest; empty: all); false (with which and why) if one can't be played there
bool placeNotes(const std::vector<int>& pitches, const std::vector<int>& tuning, const std::vector<int>& strings,
                std::vector<NeckStep>& out, std::string& error);

// How to say where a note is played, for someone who's never played: "the open high E string", "the 3rd fret of the
// B string"
// On a piano (core/drill isPianoTuning), the key by middle C: "middle C", "the E above middle C", "the low G (G3)"
std::string notePlaceText(const NeckStep& note, const std::vector<int>& tuning);
std::string keyPlaceText(int pitch);

// What's kept: runs played and passed
struct NoteQuizStats {
    int runs = 0;
    int passed = 0;
};
NoteQuizStats loadNoteQuizStats(const std::string& path);
bool saveNoteQuizStats(const std::string& path, const NoteQuizStats& stats, std::string& error);
