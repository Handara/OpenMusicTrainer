#pragma once

#include <random>
#include <string>
#include <vector>

// Interval ear training: the logic only (which questions to ask, what unlocks, progress on disk).
// Playing the sounds and drawing the answer buttons is the learn screen's job.

struct IntervalInfo {
    int semitones;
    const char* shortName; // "M3"
    const char* name;      // "Major 3rd"
};

const int INTERVAL_COUNT = 12;
// From a half step (1 semitone) to an octave (12)
const IntervalInfo& intervalInfo(int semitones);

// The order new intervals unlock in: very different sounds first, easily confused ones (m2, M7, tritone) last
const int INTERVAL_UNLOCK_ORDER[INTERVAL_COUNT] = { 7, 4, 12, 5, 3, 2, 9, 10, 8, 1, 11, 6 };
const int STARTING_INTERVALS = 2;
const int UNLOCK_WINDOW = 10;   // look at the last 10 answers...
const int UNLOCK_CORRECT = 9;   // ...and unlock the next interval once 9 of them are right

enum class IntervalDirection { Ascending, Descending, Harmonic }; // low then high, high then low, together

struct IntervalQuestion {
    int semitones;
    int firstPitch;  // MIDI pitches, as they sound; for harmonic questions both play at once
    int secondPitch;
};

struct IntervalStats {
    int asked = 0;
    int correct = 0;
};

struct IntervalProgress {
    int unlockedCount = STARTING_INTERVALS;
    IntervalStats stats[INTERVAL_COUNT + 1]; // indexed by semitones, 1..12
    std::vector<bool> recent;                // the latest answers (true = right), up to UNLOCK_WINDOW
    int bestStreak = 0;
};

struct IntervalTrainer {
    IntervalDirection direction = IntervalDirection::Ascending;
    IntervalProgress progress;
    std::mt19937 rng;
    int streak = 0; // current run of right answers
};

std::vector<int> unlockedIntervals(const IntervalProgress& progress); // semitones, in unlock order

IntervalQuestion nextIntervalQuestion(IntervalTrainer& trainer);

struct IntervalAnswerResult {
    bool correct;
    int unlocked; // semitones of an interval this answer unlocked, 0 if none
};
IntervalAnswerResult answerInterval(IntervalTrainer& trainer, const IntervalQuestion& question, int answeredSemitones);

// Progress files load leniently, like settings: they're the player's own, and losing them is worse than a bad line
IntervalProgress loadIntervalProgress(const std::string& path);
bool saveIntervalProgress(const std::string& path, const IntervalProgress& progress, std::string& error);
