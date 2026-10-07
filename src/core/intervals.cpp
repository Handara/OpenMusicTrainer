#include "core/intervals.h"

#include "core/files.h"

#include <algorithm>
#include <fstream>
#include <sstream>

const IntervalInfo INTERVALS[INTERVAL_COUNT] = {
    { 1, "m2", "Minor 2nd" }, { 2, "M2", "Major 2nd" }, { 3, "m3", "Minor 3rd" }, { 4, "M3", "Major 3rd" },
    { 5, "P4", "Perfect 4th" }, { 6, "TT", "Tritone" }, { 7, "P5", "Perfect 5th" }, { 8, "m6", "Minor 6th" },
    { 9, "M6", "Major 6th" }, { 10, "m7", "Minor 7th" }, { 11, "M7", "Major 7th" }, { 12, "P8", "Octave" },
};

const int SUPPORTED_PROGRESS_VERSION = 1;

const IntervalInfo& intervalInfo(int semitones){
    return INTERVALS[std::clamp(semitones, 1, INTERVAL_COUNT) - 1];
}

std::vector<int> unlockedIntervals(const IntervalConfig& config, const IntervalProgress& progress){
    int poolSize = (int)config.pool.size();
    int count = std::clamp(progress.unlockedCount, std::min(config.startCount, poolSize), poolSize);
    return std::vector<int>(config.pool.begin(), config.pool.begin() + count);
}

IntervalQuestion nextIntervalQuestion(IntervalTrainer& trainer){
    const IntervalConfig& config = trainer.config;
    std::vector<int> intervals = unlockedIntervals(config, trainer.progress);

    // Weak intervals come up more: weight grows with how often an interval is answered wrong.
    // Accuracy starts from 1 right out of 2 (a neutral guess) so a new interval isn't judged on 1 answer.
    // The newest interval gets double weight, so it's practiced right away.
    std::vector<double> weights;
    for (int semitones : intervals){
        const IntervalStats& stats = trainer.progress.stats[semitones];
        double accuracy = (stats.correct + 1.0) / (stats.asked + 2.0);
        double weight = 1.0 + 4.0 * (1.0 - accuracy);
        if (semitones == intervals.back() && (int)intervals.size() > config.startCount) weight *= 2.0;
        weights.push_back(weight);
    }
    std::discrete_distribution<int> pickInterval(weights.begin(), weights.end());
    int semitones = intervals[pickInterval(trainer.rng)];
    int root = std::uniform_int_distribution<int>(config.lowestRoot, config.highestRoot)(trainer.rng);

    IntervalQuestion question = { semitones, root, root + semitones };
    if (config.direction == IntervalDirection::Descending) std::swap(question.firstPitch, question.secondPitch);
    return question;
}

IntervalAnswerResult answerInterval(IntervalTrainer& trainer, const IntervalQuestion& question, int answeredSemitones){
    IntervalProgress& progress = trainer.progress;
    const IntervalConfig& config = trainer.config;
    IntervalAnswerResult result = { answeredSemitones == question.semitones, 0 };

    IntervalStats& stats = progress.stats[question.semitones];
    stats.asked++;
    if (result.correct) stats.correct++;
    trainer.streak = result.correct ? trainer.streak + 1 : 0;
    progress.bestStreak = std::max(progress.bestStreak, trainer.streak);

    progress.recent.push_back(result.correct);
    if ((int)progress.recent.size() > config.unlockWindow) progress.recent.erase(progress.recent.begin());
    int recentCorrect = (int)std::count(progress.recent.begin(), progress.recent.end(), true);
    int unlockedNow = (int)unlockedIntervals(config, progress).size();
    if ((int)progress.recent.size() >= config.unlockWindow && recentCorrect >= config.unlockCorrect && unlockedNow < (int)config.pool.size()){
        result.unlocked = config.pool[unlockedNow];
        progress.unlockedCount = unlockedNow + 1;
        progress.recent.clear(); // earn the next one with the new interval in the mix
    }
    return result;
}

IntervalProgress loadIntervalProgress(const std::string& path){
    IntervalProgress progress;
    std::ifstream file(path);
    std::string line;
    while (std::getline(file, line)){
        if (!line.empty() && line.back() == '\r') line.pop_back();
        std::istringstream ss(line);
        std::string key;
        if (!(ss >> key) || key[0] == '#') continue;
        if (key == "unlocked"){
            ss >> progress.unlockedCount;
            progress.unlockedCount = std::clamp(progress.unlockedCount, 0, INTERVAL_COUNT); // the exercise's own limits apply on use
        } else if (key == "best_streak"){
            ss >> progress.bestStreak;
        } else if (key == "recent"){
            std::string answers;
            ss >> answers;
            for (char c : answers) if (c == '0' || c == '1') progress.recent.push_back(c == '1');
            if ((int)progress.recent.size() > MAX_UNLOCK_WINDOW) progress.recent.erase(progress.recent.begin(), progress.recent.end() - MAX_UNLOCK_WINDOW);
        } else if (key == "stats"){
            int semitones, asked, correct;
            if (ss >> semitones >> asked >> correct && semitones >= 1 && semitones <= INTERVAL_COUNT && asked >= correct && correct >= 0){
                progress.stats[semitones] = { asked, correct };
            }
        }
        // anything else (a version line, a key from a newer version) is skipped
    }
    return progress;
}

bool saveIntervalProgress(const std::string& path, const IntervalProgress& progress, std::string& error){
    std::ostringstream out;
    out << "# lahn progress: interval ear training\n";
    out << "version " << SUPPORTED_PROGRESS_VERSION << "\n";
    out << "unlocked " << progress.unlockedCount << "\n";
    out << "best_streak " << progress.bestStreak << "\n";
    out << "recent ";
    for (bool right : progress.recent) out << (right ? '1' : '0');
    out << "\n# stats <semitones> <asked> <correct>\n";
    for (int semitones = 1; semitones <= INTERVAL_COUNT; semitones++){
        const IntervalStats& stats = progress.stats[semitones];
        if (stats.asked > 0) out << "stats " << semitones << " " << stats.asked << " " << stats.correct << "\n";
    }
    return writeFileAtomically(path, out.str(), error);
}
