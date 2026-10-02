#include "core/positions.h"

#include <algorithm>
#include <cstdlib>

std::vector<StringFret> positionsOf(int pitch, const std::vector<int>& tuning, int maxFret){
    std::vector<StringFret> places;
    for (int string = 0; string < (int)tuning.size(); string++){
        int fret = pitch - tuning[string];
        if (fret >= 0 && fret <= maxFret) places.push_back({string, fret});
    }
    return places;
}

StringFret likeliestPosition(const std::vector<StringFret>& places, StringFret last){
    if (places.empty()) return {-1, -1};
    StringFret best = places[0];
    auto distance = [&](const StringFret& place){
        if (last.string < 0) return place.fret * 100;                       // no history: the lowest fret
        // The hand moves along the neck much more than it changes string: frets weigh more (an open string is at
        // fret 0, as far from a hand up the neck as the nut is)
        return std::abs(place.fret - last.fret) * 10 + std::abs(place.string - last.string);
    };
    for (const StringFret& place : places) if (distance(place) < distance(best)) best = place;
    return best;
}

std::vector<StringFret> chordPositions(const std::vector<int>& pitches, const std::vector<int>& tuning, int maxFret, StringFret last){
    std::vector<StringFret> chord;
    for (int pitch : pitches){
        std::vector<StringFret> places = positionsOf(pitch, tuning, maxFret);
        // A string holds one note at a time
        places.erase(std::remove_if(places.begin(), places.end(), [&](const StringFret& place){
            return std::any_of(chord.begin(), chord.end(), [&](const StringFret& taken){ return taken.string == place.string; });
        }), places.end());
        StringFret place = likeliestPosition(places, last);
        chord.push_back(place);
        if (place.string >= 0) last = place; // the next note is looked for near this one
    }
    return chord;
}

FretSpan fretSpanFor(const std::vector<int>& frets, int minFrets, int maxFret){
    if (frets.empty()) return {0, std::min(minFrets, maxFret)};
    int lowest = *std::min_element(frets.begin(), frets.end()), highest = *std::max_element(frets.begin(), frets.end());
    FretSpan span;
    span.first = lowest <= 3 ? 0 : lowest - 1;
    span.last = std::min(maxFret, std::max(highest + 1, span.first + minFrets));
    span.first = std::max(0, std::min(span.first, span.last - minFrets)); // near the top of the neck: widened down
    return span;
}
