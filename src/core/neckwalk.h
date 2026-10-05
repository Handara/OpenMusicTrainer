#pragma once

#include "core/necktrainer.h"

#include <array>
#include <random>
#include <string>
#include <vector>

// Neck walk, a game: one note at a time, found on strings side by side, walked from the highest of them down and back
// up, to the beat of a tune in that note's key. Pure logic: the rounds, where the note is, when each of its notes is
// due, and how the player did; the screen plays the tune, the computer's notes and the crowd (learn/neckwalkexercise).
//
// After a bar to get ready, a round is four bars, two licks of the tune: in the first the computer plays the walk, a
// note every `beatsPerNote` beats, shown on the neck; in the second the player plays it back, from memory, at the
// same pace. Half a beat before the round ends, the verdict: the crowd cheering a walk all right, clapping politely at
// one at least half right, going "awww" at less. The next round's note is a fourth up (round the circle of fourths).
// Less than half right costs a life; with none left, it's over.

struct NeckWalkLevel {
    const char* name;           // "Easy"
    const char* key;            // "easy": in files
    int minStrings, maxStrings; // how many strings the note is walked across
    int notes;                  // notes in a walk
    double beatsPerNote;
    int maxFret;                // the places stay at or below it
    float windowSeconds;        // how far from its beat a note still counts, either way
    int points;                 // what a note right is worth, times the streak plus one
};
const int NECK_WALK_LEVELS = 3;
const NeckWalkLevel& neckWalkLevel(int index); // 0 easy, 1 normal, 2 hard
int neckWalkLevelIndex(const std::string& key); // -1 if it isn't one

const int NECK_WALK_LIVES = 5;
const int NECK_WALK_BAR_BEATS = 4;
const int NECK_WALK_INTRO_BEATS = 4;  // the bar before the first round
const int NECK_WALK_PART_BEATS = 8;   // two bars: the computer's lick, then the player's
const int NECK_WALK_ROUND_BEATS = 2 * NECK_WALK_PART_BEATS;

// A round's walk: the note (a pitch class) on `count` strings from `firstString` up, each place the nearest to the
// one before (the first nearest `nearFret`), none above `maxFret`; walked from the highest string down and back up,
// bouncing between the outer strings until there are `notes` (3 strings: G D A D G; 2: D A D A D). Empty if the
// note isn't on one of the strings there.
std::vector<NeckStep> neckWalkSteps(int pitchClass, const std::vector<int>& tuning, int firstString, int count, int nearFret,
                                    int maxFret, int notes);

enum class WalkNote { Due, Right, Wrong, Missed };
enum class NeckWalkVerdict { Cheer, Claps, Aww };

struct NeckWalkGame {
    int levelIndex = 0;
    NeckWalkLevel level = neckWalkLevel(0);
    std::vector<int> tuning;
    std::mt19937 random;
    double startTime = 0.0;      // the tune's first beat, a bar before the first round's (seconds, the game's clock)
    double beatSeconds = 0.5;
    float tempo = 120.0f;
    int round = 0;               // from 0
    int root = 0;                // the round's note, a pitch class
    std::vector<NeckStep> walk;  // where to play it, in order
    std::vector<WalkNote> notes; // how each of the player's went
    std::vector<bool> heardWrong; // another note was heard near it: it's wrong unless the right one comes in time
    bool judged = false;         // the round's verdict is in
    int nextRoot = 0;            // the next round's, known a round ahead: its first notes are played on time
    std::vector<NeckStep> nextWalk;
    int lives = 0;
    long long score = 0;
    int streak = 0, bestStreak = 0; // rounds all right in a row
    int cleared = 0;                // rounds all right
    bool over = false;
    std::array<int, 12> rightByNote{}, wrongByNote{}; // this game's walk notes, each round's note counted
};

// What happened, for the screen to show and sound
struct NeckWalkEvents {
    int right = -1, wrong = -1, missed = -1; // a walk note (its index) judged now
    bool verdict = false;                     // a round's verdict, now: which, in `how`
    NeckWalkVerdict how = NeckWalkVerdict::Aww;
    bool newRound = false;
    bool over = false;
};

void startNeckWalk(NeckWalkGame& game, int levelIndex, const std::vector<int>& tuning, unsigned seed, double startTime,
                   float tempo);
// When things are due: a round's first beat; the current round's notes, the computer's and the player's; its verdict
double neckWalkRoundStart(const NeckWalkGame& game, int round);
double neckWalkShowTime(const NeckWalkGame& game, int note);
double neckWalkNoteTime(const NeckWalkGame& game, int note);
double neckWalkVerdictTime(const NeckWalkGame& game);
// A note heard at `time`: the round's note in any octave (where it's played is the cards' to show; the detector can't
// always tell octaves) counts for the walk note due then, if it's that close to it, at once. Another note makes it
// wrong, unless the right one still comes in time (a slip, or the detector catching the string's first moment wrong).
// A note far from every beat is let go.
NeckWalkEvents neckWalkPlayed(NeckWalkGame& game, int pitch, double time);
// What the crowd makes of the round's walk as it stands: all right, at least half, or less
NeckWalkVerdict neckWalkVerdictOf(const NeckWalkGame& game);
// Time going on: walk notes not played in time are missed, the verdict comes at its beat, then the next round
NeckWalkEvents neckWalkUpdate(NeckWalkGame& game, double time);

// What's kept: each game, the walk notes right and wrong for every note, all games together, and the last choice of
// level and tempo (the next game starts with them)
struct NeckWalkRecord {
    std::string date;
    long long score = 0;
    int cleared = 0;
    int bestStreak = 0;
    std::string level = "easy"; // its key
    int bpm = 0;
};
struct NeckWalkStats {
    std::vector<NeckWalkRecord> games; // oldest first
    std::array<int, 12> right{}, wrong{};
    int lastLevel = -1; // -1: none chosen yet
    int lastBpm = 0;
};
NeckWalkStats loadNeckWalkStats(const std::string& path);
bool saveNeckWalkStats(const std::string& path, const NeckWalkStats& stats, std::string& error);
void addNeckWalkGame(NeckWalkStats& stats, const NeckWalkGame& game, const std::string& date);
long long neckWalkBest(const NeckWalkStats& stats, int levelIndex); // 0: none yet
