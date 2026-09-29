#include "core/positions.h"

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
