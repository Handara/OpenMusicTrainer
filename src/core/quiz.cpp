#include "core/quiz.h"

#include "core/files.h"

#include <algorithm>
#include <fstream>
#include <sstream>

void recordQuizAnswer(QuizProgress& progress, int& streak, bool right){
    progress.asked++;
    if (right){
        progress.correct++;
        streak++;
        progress.bestStreak = std::max(progress.bestStreak, streak);
    } else {
        streak = 0;
    }
}

QuizProgress loadQuizProgress(const std::string& path){
    QuizProgress progress;
    std::ifstream file(path);
    std::string line;
    while (std::getline(file, line)){
        std::istringstream ss(line);
        std::string key;
        if (!(ss >> key) || key[0] == '#') continue;
        int value = 0;
        if (!(ss >> value) || value < 0) continue;
        if (key == "asked") progress.asked = value;
        else if (key == "correct") progress.correct = value;
        else if (key == "best_streak") progress.bestStreak = value;
        // anything else (the version line, a key from a newer version) is skipped
    }
    progress.correct = std::min(progress.correct, progress.asked);
    return progress;
}

bool saveQuizProgress(const std::string& path, const QuizProgress& progress, std::string& error){
    std::ostringstream out;
    out << "# lahn progress: a quiz\n";
    out << "version 1\n";
    out << "asked " << progress.asked << "\n";
    out << "correct " << progress.correct << "\n";
    out << "best_streak " << progress.bestStreak << "\n";
    return writeFileAtomically(path, out.str(), error);
}
