#pragma once

#include "core/necktrainer.h"

#include <array>
#include <random>
#include <string>
#include <vector>

// Neck walk, a game: each round, a sequence of notes on the neck in a key, played by the computer and then played back
// by the player from memory, to the beat of a tune in that key. Pure logic: the rounds, their notes, where and when,
// and how the player did; the screen plays the tune, the computer's notes and the crowd (learn/neckwalkexercise).
//
// After a bar to get ready, a round is two parts, each as many bars as the sequence takes (a bar for a short one, so
// there's always something playing): the computer plays it in the first, shown on the neck; the player plays it back
// in the second, at the same rhythm, the neck bare. The tune goes on through them all, changing key where a round
// starts. Half a beat before the round ends, the verdict: the crowd cheering a sequence all right, clapping politely
// at one at least half right, going "awww" at less. Less than half right costs a life; with none left, it's over.
//
// Learning little by little: the sequences start on as many strings as the player chose (two, say), side by side, and
// every two rounds all right on that many, one more string, up to all of them. A key lasts two rounds, then the next
// is a fourth up (round the circle of fourths).
//
// The levels differ in what's played:
//   Easy:   the key's note on the strings, from the highest down and back up, a note every beat
//   Normal: the same places in other orders (skipping a string, from the outside in, at random), a mixed rhythm
//   Hard:   the key's triad (root, third, fifth) in one place on the neck, up and down, in a syncopated rhythm

enum class NeckWalkSequence { Straight, Patterns, Triads };
enum class NeckWalkRhythm { Even, Mixed, Syncopated };

struct NeckWalkLevel {
    const char* name;           // "Easy"
    const char* key;            // "easy": in files
    NeckWalkSequence sequence;
    NeckWalkRhythm rhythm;
    int maxFret;                // the places stay at or below it
    float windowSeconds;        // how far from its beat a note still counts, either way
    int points;                 // what a note right is worth, times the streak plus one
};
const int NECK_WALK_LEVELS = 3;
const NeckWalkLevel& neckWalkLevel(int index); // 0 easy, 1 normal, 2 hard
int neckWalkLevelIndex(const std::string& key); // -1 if it isn't one

const int NECK_WALK_LIVES = 5;
const int NECK_WALK_BAR_BEATS = 4;
const int NECK_WALK_INTRO_BARS = 1;   // the bar before the first round
const int NECK_WALK_MAX_NOTES = 12;   // in a sequence
const int NECK_WALK_MIN_STRINGS = 2;
const int NECK_WALK_CLEARS_TO_GROW = 2; // rounds all right on so many strings before one more
const int NECK_WALK_ROUNDS_A_KEY = 2;

// The note (a pitch class) on `count` strings from `firstString` up, each place the nearest to the one before (the
// first nearest `nearFret`), none above `maxFret`; walked from the highest string down and back up, bouncing between
// the outer strings until there are `notes` (3 strings: G D A D G). Empty if the note isn't on one of the strings there.
std::vector<NeckStep> neckWalkSteps(int pitchClass, const std::vector<int>& tuning, int firstString, int count, int nearFret,
                                    int maxFret, int notes);
// The major triad on `root` in a place on the neck: its notes on `count` strings from `firstString` up, frets
// `position` to three above it, lowest first (a pitch two strings share is played on the lower one)
std::vector<NeckStep> neckWalkTriad(int root, const std::vector<int>& tuning, int firstString, int count, int position);
// A rhythm for `notes` notes: each one's beat from the start of a part. Its part's length in bars, as few as hold it
// with a beat to spare after the last note, in `partBars`.
std::vector<double> neckWalkRhythm(NeckWalkRhythm rhythm, int notes, std::mt19937& random, int& partBars);

enum class WalkNote { Due, Right, Wrong, Missed };
enum class NeckWalkVerdict { Cheer, Claps, Aww };

// A round: its key, its sequence, its rhythm, and where it is in the tune
struct NeckWalkRound {
    int root = 0;                // a pitch class
    std::vector<NeckStep> walk;  // the notes, in order
    std::vector<double> onsets;  // each one's beat from the start of a part, the same for the computer and the player
    int partBars = 1;            // the computer's part, then the player's, this many bars each
    int firstBar = 0;            // counted from the tune's first
    int strings = 0;             // how many strings it goes across
};

struct NeckWalkGame {
    int levelIndex = 0;
    NeckWalkLevel level = neckWalkLevel(0);
    std::vector<int> tuning;
    std::mt19937 random;
    double startTime = 0.0;      // the tune's first beat, a bar before the first round's (seconds, the game's clock)
    double beatSeconds = 0.5;
    float tempo = 120.0f;
    int round = 0;               // from 0
    int firstRoot = 0;           // the first round's key
    int startStrings = 2;        // as chosen
    int strings = 2;             // the strings the rounds being made go across: more as the player gets them right
    int clearsOnStrings = 0;     //   rounds all right on that many so far
    int mostStrings = 0;         // the most a round has gone across, this game
    NeckWalkRound now;           // this round
    NeckWalkRound next;          // the next, known a round ahead: its first notes are played on time
    std::vector<WalkNote> notes; // how each of the player's went
    std::vector<bool> heardWrong; // another note was heard near it: it's wrong unless the right one comes in time
    bool judged = false;         // the round's verdict is in
    int lives = 0;
    long long score = 0;
    int streak = 0, bestStreak = 0; // rounds all right in a row
    int cleared = 0;                // rounds all right
    bool over = false;
    std::array<int, 12> rightByNote{}, wrongByNote{}; // this game's notes, counted by their round's key
};

// What happened, for the screen to show and sound
struct NeckWalkEvents {
    int right = -1, wrong = -1, missed = -1; // a note of the player's (its index) judged now
    bool verdict = false;                     // a round's verdict, now: which, in `how`
    NeckWalkVerdict how = NeckWalkVerdict::Aww;
    bool newRound = false;
    bool moreStrings = false;                 // from the round after next, one string more
    bool over = false;
};

// `strings`: how many to start on (kept between two and the instrument's)
void startNeckWalk(NeckWalkGame& game, int levelIndex, int strings, const std::vector<int>& tuning, unsigned seed,
                   double startTime, float tempo);
// When things are due (seconds, the game's clock): a bar of the tune; a round's start; a round's notes as the computer
// plays them; this round's as the player should; this round's verdict
double neckWalkBarTime(const NeckWalkGame& game, int bar);
double neckWalkRoundStart(const NeckWalkGame& game, const NeckWalkRound& round);
double neckWalkShowTime(const NeckWalkGame& game, const NeckWalkRound& round, int note);
double neckWalkNoteTime(const NeckWalkGame& game, int note);
double neckWalkVerdictTime(const NeckWalkGame& game);
// A note heard at `time`: the note due then, in any octave (where it's played is the cards' to show; the detector
// can't always tell octaves), counts if it's that close to it, at once. Another note makes it wrong, unless the right
// one still comes in time (a slip, or the detector catching the string's first moment wrong). A note far from every
// beat is let go, and so is anything played while the computer plays.
NeckWalkEvents neckWalkPlayed(NeckWalkGame& game, int pitch, double time);
// What the crowd makes of the round's sequence as it stands: all right, at least half, or less
NeckWalkVerdict neckWalkVerdictOf(const NeckWalkGame& game);
// Time going on: notes not played in time are missed, the verdict comes at its beat, then the next round
NeckWalkEvents neckWalkUpdate(NeckWalkGame& game, double time);

// What's kept: each game, the notes right and wrong for every key, all games together, and the last choice of level
// and tempo (the next game starts with them)
struct NeckWalkRecord {
    std::string date;
    long long score = 0;
    int cleared = 0;
    int bestStreak = 0;
    std::string level = "easy"; // its key
    int bpm = 0;
    int strings = 0;            // the most a round went across
};
struct NeckWalkStats {
    std::vector<NeckWalkRecord> games; // oldest first
    std::array<int, 12> right{}, wrong{};
    int lastLevel = -1; // -1: none chosen yet
    int lastBpm = 0;
    int lastStrings = 0; // to start on
};
NeckWalkStats loadNeckWalkStats(const std::string& path);
bool saveNeckWalkStats(const std::string& path, const NeckWalkStats& stats, std::string& error);
void addNeckWalkGame(NeckWalkStats& stats, const NeckWalkGame& game, const std::string& date);
long long neckWalkBest(const NeckWalkStats& stats, int levelIndex); // 0: none yet
