#pragma once

#include <string>

// Progress in a quiz, where each answer is right or wrong (finding notes on the fretboard, singing notes): how many
// were asked, how many were right, and the longest run of right answers.
struct QuizProgress {
    int asked = 0;
    int correct = 0;
    int bestStreak = 0;
};

// Records an answer, given the current run of right answers (updated too)
void recordQuizAnswer(QuizProgress& progress, int& streak, bool right);

// Progress files load leniently, like the other progress files: they're the player's own
QuizProgress loadQuizProgress(const std::string& path);
bool saveQuizProgress(const std::string& path, const QuizProgress& progress, std::string& error);
