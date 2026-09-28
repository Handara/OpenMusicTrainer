#pragma once

#include <random>
#include <string>
#include <vector>

// Fretboard note finding: "C on the A string", answered by clicking the fret or playing it. The logic only (which
// questions, what counts as right, progress on disk); drawing the fretboard and listening are the exercise's job.

struct FretboardConfig {
    std::vector<int> tuning = { 40, 45, 50, 55, 59, 64 }; // MIDI pitch per string, lowest first
    std::vector<int> strings;      // which strings are asked (0 = lowest); empty = all of them
    int lowestFret = 0;
    int highestFret = 12;
    bool naturalsOnly = true;      // only C D E F G A B; otherwise the sharps and flats too
};

struct FretboardQuestion {
    int stringIndex;
    int pitchClass;  // 0 = C ... 11 = B
};

struct FretboardProgress {
    int asked = 0;
    int correct = 0;
    int bestStreak = 0;
};

struct FretboardTrainer {
    FretboardConfig config;
    FretboardProgress progress;
    std::mt19937 rng;
    int streak = 0;                 // right answers in a row
    FretboardQuestion last{-1, -1}; // never the same question twice in a row
};

// A note that has a fret on the string in the config's range, never the last question again (when there's a choice)
FretboardQuestion nextFretboardQuestion(FretboardTrainer& trainer);

// The frets where the answer is, in the config's range: often one, two when both an open string and its 12th fret
// fit (both are the same note an octave apart)
std::vector<int> fretboardAnswers(const FretboardConfig& config, const FretboardQuestion& question);

// Clicking a fret on the asked string: right if it's one of the answers
bool fretIsRight(const FretboardConfig& config, const FretboardQuestion& question, int fret);
// Playing a note: right if it's the pitch of one of the answers (the same pitch on another string sounds the same,
// so it counts too)
bool pitchIsRight(const FretboardConfig& config, const FretboardQuestion& question, int pitch);

// Records an answer: the counts, the streak and the best streak
void recordFretboardAnswer(FretboardTrainer& trainer, bool right);

// Progress files load leniently, like the other progress files: they're the player's own
FretboardProgress loadFretboardProgress(const std::string& path);
bool saveFretboardProgress(const std::string& path, const FretboardProgress& progress, std::string& error);
