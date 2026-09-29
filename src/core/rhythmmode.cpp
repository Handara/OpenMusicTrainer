#include "core/rhythmmode.h"

#include <algorithm>

std::vector<RhythmHit> rhythmHits(const Chart& chart, int part){
    // Every note of the part as (tick, pitch), whatever the instrument
    std::vector<std::pair<int, int>> notes;
    if (part >= 0 && part < partCount(chart)){
        if (isKeysPart(chart, part)){
            for (const KeysNote& note : chart.keysTracks[part - chart.frettedTracks.size()].notes) notes.push_back({note.tick, note.pitch});
        } else {
            const FrettedTrack& track = chart.frettedTracks[part];
            for (const FrettedNote& note : track.notes) notes.push_back({note.tick, track.tuning[note.stringIndex] + note.fret});
        }
    }
    std::sort(notes.begin(), notes.end());

    // One moment per tick: its highest note, and how many notes it has
    struct Moment { int tick; int top; int count; };
    std::vector<Moment> moments;
    for (const auto& [tick, pitch] : notes){
        if (!moments.empty() && moments.back().tick == tick){
            moments.back().top = std::max(moments.back().top, pitch);
            moments.back().count++;
        } else {
            moments.push_back({tick, pitch, 1});
        }
    }
    if (moments.empty()) return {};

    // The middle pitch splits dons from kas
    std::vector<int> tops;
    for (const Moment& moment : moments) tops.push_back(moment.top);
    std::nth_element(tops.begin(), tops.begin() + tops.size() / 2, tops.end());
    int middle = tops[tops.size() / 2];

    std::vector<RhythmHit> hits;
    for (const Moment& moment : moments){
        hits.push_back({moment.tick, moment.top >= middle ? RhythmHitKind::Ka : RhythmHitKind::Don, moment.count > 1});
    }
    return hits;
}
