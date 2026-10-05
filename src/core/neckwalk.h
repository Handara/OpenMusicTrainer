#pragma once

#include "core/necktrainer.h"

#include <array>
#include <random>
#include <string>
#include <vector>

// Neck walk, a game: one note at a time, found on two or three strings side by side, walked from the highest of them
// down and back up, to the beat of a tune in that note's key. Pure logic: the rounds, where the note is, when each of
// its notes is due, and how the player did; the screen plays the tune and the crowd (learn/neckwalkgame).
//
// A round is one phrase of the tune, four bars: the first is the call (the tune in the new key, the places shown),
// then the walk, a note every `beatsPerNote` beats from the second bar's first beat, the last one landing on the
// phrase's last bar; then the verdict, the crowd cheering a round all right or sighing at one that wasn't. The next
// note is a fourth up (round the circle of fourths). A round wrong costs a life; with none left, the game is over.

struct NeckWalkLevel {
    const char* name;
    int minStrings, maxStrings; // how many strings the note is walked across
    int notes;                  // notes in a walk
    int beatsPerNote;
    int maxFret;                // the places stay at or below it
    float windowSeconds;        // how far from its beat a note still counts, either way
    int lives;
};
const NeckWalkLevel& neckWalkEasy();

const int NECK_WALK_BAR_BEATS = 4;
const int NECK_WALK_ROUND_BARS = 4;
const int NECK_WALK_ROUND_BEATS = NECK_WALK_BAR_BEATS * NECK_WALK_ROUND_BARS;

// A round's walk: the note (a pitch class) on `count` strings from `firstString` up, each place the nearest to the
// one before (the first nearest `nearFret`), none above `maxFret`; walked from the highest string down and back up,
// bouncing between the outer strings until there are `notes` (3 strings: G D A D G; 2: D A D A D). Empty if the
// note isn't on one of the strings there.
std::vector<NeckStep> neckWalkSteps(int pitchClass, const std::vector<int>& tuning, int firstString, int count, int nearFret,
                                    int maxFret, int notes);

enum class WalkNote { Due, Right, Wrong, Missed };

struct NeckWalkGame {
    NeckWalkLevel level;
    std::vector<int> tuning;
    std::mt19937 random;
    double startTime = 0.0;      // the first round's first beat (seconds, the game's clock)
    double beatSeconds = 0.5;
    int round = 0;               // from 0
    int root = 0;                // the round's note, a pitch class
    std::vector<NeckStep> walk;  // where to play it, in order
    std::vector<WalkNote> notes; // how each of them went
    std::vector<bool> heardWrong; // another note was heard near it: it's wrong unless the right one comes in time
    bool judged = false;         // the round's verdict is in
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
    bool cheer = false, aww = false;          // a round's verdict
    bool newRound = false;
    bool over = false;
};

void startNeckWalk(NeckWalkGame& game, const NeckWalkLevel& level, const std::vector<int>& tuning, unsigned seed,
                   double startTime, float tempo);
// When things are due: a round's first beat, and its walk notes and verdict
double neckWalkRoundStart(const NeckWalkGame& game, int round);
double neckWalkNoteTime(const NeckWalkGame& game, int note);
double neckWalkVerdictTime(const NeckWalkGame& game);
// A note heard at `time`: the round's note in any octave (where it's played is the cards' to show; the detector can't
// always tell octaves) counts for the walk note due then, if it's that close to it, at once. Another note makes it
// wrong, unless the right one still comes in time (a slip, or the detector catching the string's first moment wrong).
// A note far from every beat is let go.
NeckWalkEvents neckWalkPlayed(NeckWalkGame& game, int pitch, double time);
// Time going on: walk notes not played in time are missed, the verdict comes at its beat, then the next round
NeckWalkEvents neckWalkUpdate(NeckWalkGame& game, double time);

// What's kept: each game, and the walk notes right and wrong for every note, all games together
struct NeckWalkRecord {
    std::string date;
    long long score = 0;
    int cleared = 0;
    int bestStreak = 0;
};
struct NeckWalkStats {
    std::vector<NeckWalkRecord> games; // oldest first
    std::array<int, 12> right{}, wrong{};
};
NeckWalkStats loadNeckWalkStats(const std::string& path);
bool saveNeckWalkStats(const std::string& path, const NeckWalkStats& stats, std::string& error);
void addNeckWalkGame(NeckWalkStats& stats, const NeckWalkGame& game, const std::string& date);
long long neckWalkBest(const NeckWalkStats& stats); // 0: none yet
