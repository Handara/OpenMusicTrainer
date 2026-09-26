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

enum class IntervalDirection { Ascending, Descending, Harmonic }; // low then high, high then low, together

const int MAX_UNLOCK_WINDOW = 50;

// The rules of one interval exercise. Exercise files set these (see core/exercisefile.h);
// the defaults are the classic full course.
struct IntervalConfig {
    IntervalDirection direction = IntervalDirection::Ascending;
    // The intervals practiced, in the order they unlock: very different sounds first,
    // easily confused ones (m2, M7, tritone) last
    std::vector<int> pool = { 7, 4, 12, 5, 3, 2, 9, 10, 8, 1, 11, 6 };
    int startCount = 2;       // unlocked from the start
    int unlockCorrect = 9;    // the next interval unlocks once this many...
    int unlockWindow = 10;    // ...of the last this many answers are right
    int lowestRoot = 48;      // questions start on a random note in this range (MIDI; C3)
    int highestRoot = 67;     // (G4: plus an octave is G5)
    float gapSeconds = 0.7f;  // between the two notes, when they're played one after the other
};

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
    int unlockedCount = 0;                   // 0 = just started: the config's startCount applies
    IntervalStats stats[INTERVAL_COUNT + 1]; // indexed by semitones, 1..12
    std::vector<bool> recent;                // the latest answers (true = right), up to the unlock window
    int bestStreak = 0;
};

struct IntervalTrainer {
    IntervalConfig config;
    IntervalProgress progress;
    std::mt19937 rng;
    int streak = 0; // current run of right answers
};

std::vector<int> unlockedIntervals(const IntervalConfig& config, const IntervalProgress& progress); // in unlock order

IntervalQuestion nextIntervalQuestion(IntervalTrainer& trainer);

struct IntervalAnswerResult {
    bool correct;
    int unlocked; // semitones of an interval this answer unlocked, 0 if none
};
IntervalAnswerResult answerInterval(IntervalTrainer& trainer, const IntervalQuestion& question, int answeredSemitones);

// Progress files load leniently, like settings: they're the player's own, and losing them is worse than a bad line
IntervalProgress loadIntervalProgress(const std::string& path);
bool saveIntervalProgress(const std::string& path, const IntervalProgress& progress, std::string& error);
