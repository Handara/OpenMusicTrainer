#include "core/tuningcheck.h"

#include <algorithm>
#include <cmath>

float centsOff(float midi, int note){
    float semitones = std::fmod(midi - note, 12.0f);
    if (semitones > 6.0f) semitones -= 12.0f;
    if (semitones < -6.0f) semitones += 12.0f;
    return semitones * 100.0f;
}

int openStringHeard(float midi, const std::vector<int>& tuning, float range){
    int best = -1;
    float bestDistance = range;
    for (int i = 0; i < (int)tuning.size(); i++){
        // The note itself, or an octave either side of it: the nearest of the three
        for (int octave : { -12, 0, 12 }){
            float distance = std::fabs(midi - (float)(tuning[i] + octave));
            if (distance <= bestDistance){
                bestDistance = distance;
                best = i;
            }
        }
    }
    return best;
}

TuningCheck startTuningCheck(const std::vector<int>& tuning){
    TuningCheck check;
    check.tuning = tuning;
    check.strings.assign(tuning.size(), StringCheck{});
    return check;
}

void hearForTuning(TuningCheck& check, float midi, float seconds){
    if (midi <= 0.0f) return;
    int string = openStringHeard(midi, check.tuning);
    if (string < 0) return;
    // Another string: the one before stops counting its time in tune
    if (check.lastString >= 0 && check.lastString != string) check.strings[check.lastString].inTuneFor = 0.0f;
    check.lastString = string;
    StringCheck& heard = check.strings[string];
    heard.heard = true;
    heard.cents = centsOff(midi, check.tuning[string]);
    heard.inTuneFor = std::fabs(heard.cents) <= IN_TUNE_CENTS ? heard.inTuneFor + seconds : 0.0f;
    if (heard.inTuneFor >= IN_TUNE_HOLD_S) heard.tuned = true;
}

bool allTuned(const TuningCheck& check){
    if (check.strings.empty()) return false;
    for (const StringCheck& string : check.strings) if (!string.tuned) return false;
    return true;
}

void watchTuning(TuningWatch& watch, float offsetCents){
    if (std::fabs(offsetCents) > TUNING_WATCH_RANGE) return;
    watch.offsets.push_back(offsetCents);
    if ((int)watch.offsets.size() > TUNING_WATCH_NOTES) watch.offsets.erase(watch.offsets.begin());
}

bool looksOutOfTune(const TuningWatch& watch, float& cents){
    if ((int)watch.offsets.size() < TUNING_WATCH_NOTES) return false;
    std::vector<float> sorted = watch.offsets;
    std::sort(sorted.begin(), sorted.end());
    float median = (sorted[TUNING_WATCH_NOTES / 2 - 1] + sorted[TUNING_WATCH_NOTES / 2]) / 2.0f;
    if (std::fabs(median) < OUT_OF_TUNE_CENTS) return false;
    // Most of them off the same way, not a median dragged there by a few bends
    int sameWay = 0;
    for (float offset : watch.offsets) if (offset * median > 0.0f && std::fabs(offset) >= OUT_OF_TUNE_CENTS / 2) sameWay++;
    if (sameWay * 4 < TUNING_WATCH_NOTES * 3) return false;
    cents = median;
    return true;
}
