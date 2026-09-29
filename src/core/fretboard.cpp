#include "core/fretboard.h"

#include <algorithm>

const int NATURALS[7] = { 0, 2, 4, 5, 7, 9, 11 };

std::vector<int> fretboardAnswers(const FretboardConfig& config, const FretboardQuestion& question){
    std::vector<int> frets;
    if (question.stringIndex < 0 || question.stringIndex >= (int)config.tuning.size()) return frets;
    int open = config.tuning[question.stringIndex];
    for (int fret = config.lowestFret; fret <= config.highestFret; fret++){
        if ((open + fret) % 12 == question.pitchClass) frets.push_back(fret);
    }
    return frets;
}

bool fretIsRight(const FretboardConfig& config, const FretboardQuestion& question, int fret){
    std::vector<int> answers = fretboardAnswers(config, question);
    return std::find(answers.begin(), answers.end(), fret) != answers.end();
}

bool pitchIsRight(const FretboardConfig& config, const FretboardQuestion& question, int pitch){
    for (int fret : fretboardAnswers(config, question)){
        if (config.tuning[question.stringIndex] + fret == pitch) return true;
    }
    return false;
}

FretboardQuestion nextFretboardQuestion(FretboardTrainer& trainer){
    const FretboardConfig& config = trainer.config;
    std::vector<int> strings = config.strings;
    if (strings.empty()) for (int s = 0; s < (int)config.tuning.size(); s++) strings.push_back(s);

    // Every question the config allows, then one at random (not the last one, if there's anything else)
    std::vector<FretboardQuestion> candidates;
    for (int s : strings){
        for (int pitchClass = 0; pitchClass < 12; pitchClass++){
            if (config.naturalsOnly && !std::count(std::begin(NATURALS), std::end(NATURALS), pitchClass)) continue;
            FretboardQuestion question{s, pitchClass};
            if (fretboardAnswers(config, question).empty()) continue;
            if (s == trainer.last.stringIndex && pitchClass == trainer.last.pitchClass) continue;
            candidates.push_back(question);
        }
    }
    if (candidates.empty()) return trainer.last; // a one-question exercise: ask it again
    std::uniform_int_distribution<int> pick(0, (int)candidates.size() - 1);
    trainer.last = candidates[pick(trainer.rng)];
    return trainer.last;
}
