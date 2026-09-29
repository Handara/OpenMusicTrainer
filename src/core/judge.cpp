#include "core/judge.h"

#include <algorithm>
#include <cmath>

static bool matches(const PlayNote& note, const PlayerInput& input){
    if (input.pitch >= 0) return note.pitch == input.pitch;
    if (input.stringIndex >= 0) return note.stringIndex == input.stringIndex;
    return false;
}

JudgeResult judgeInput(std::vector<PlayNote>& notes, const PlayerInput& input){
    JudgeResult result;

    // Only notes within the near window can match: binary-search the window's start (notes are sorted)
    auto first = std::lower_bound(notes.begin(), notes.end(), input.time - NEAR_WINDOW_S,
                                  [](const PlayNote& note, double time){ return note.time < time; });
    PlayNote* nearest = nullptr;
    for (auto it = first; it != notes.end() && it->time <= input.time + NEAR_WINDOW_S; ++it){
        if (it->judged || !matches(*it, input)) continue;
        if (!nearest || std::fabs(it->time - input.time) < std::fabs(nearest->time - input.time)) nearest = &*it;
    }
    if (!nearest) return result;

    result.error = nearest->time - input.time;
    result.pitch = nearest->pitch;
    bool perfect = std::fabs(result.error) <= PERFECT_WINDOW_S;
    result.judgement = perfect ? Judgement::Perfect : Judgement::Near;

    auto markHit = [&](PlayNote& note){
        note.judged = true;
        note.hit = true;
        note.wasPerfect = perfect;
        note.hitFlash = HIT_FLASH_DURATION;
        result.notesHit++;
    };
    markHit(*nearest);
    if (input.pitch >= 0){
        // The rest of a chord: unjudged notes at the same moment
        for (auto it = first; it != notes.end() && it->time <= input.time + NEAR_WINDOW_S; ++it){
            if (!it->judged && std::fabs(it->time - nearest->time) < 0.001) markHit(*it);
        }
    }
    return result;
}

int markMisses(std::vector<PlayNote>& notes, double now){
    // Everything before this point is too late to hit (notes are sorted by time)
    auto end = std::lower_bound(notes.begin(), notes.end(), now - NEAR_WINDOW_S,
                                [](const PlayNote& note, double time){ return note.time < time; });
    int missed = 0;
    for (auto it = notes.begin(); it != end; ++it){
        if (it->judged) continue;
        it->judged = true;
        missed++;
    }
    return missed;
}
