#include "core/reading.h"

#include "core/scales.h"

#include <algorithm>

std::vector<DrillNote> readingPositionNotes(const ReadingConfig& config){
    std::vector<DrillNote> found;
    const ScaleInfo* scale = findScale(config.scale);
    if (!scale) return found;
    std::vector<int> strings = config.strings;
    if (strings.empty()) for (int s = 0; s < (int)config.tuning.size(); s++) strings.push_back(s);
    for (int s : strings){
        if (s < 0 || s >= (int)config.tuning.size()) continue;
        for (int fret = config.lowestFret; fret <= config.highestFret; fret++){
            int pitch = config.tuning[s] + fret;
            int degree = ((pitch - config.rootPitchClass) % 12 + 12) % 12;
            if (std::find(scale->steps.begin(), scale->steps.end(), degree) == scale->steps.end()) continue;
            // A pitch already found keeps whichever place has the lower fret
            auto same = std::find_if(found.begin(), found.end(), [&](const DrillNote& note){ return note.pitch == pitch; });
            if (same == found.end()) found.push_back({0.0, s, fret, pitch});
            else if (fret < same->fret) *same = {0.0, s, fret, pitch};
        }
    }
    std::sort(found.begin(), found.end(), [](const DrillNote& a, const DrillNote& b){ return a.pitch < b.pitch; });
    return found;
}

bool buildReading(const ReadingConfig& config, std::mt19937& rng, std::vector<DrillNote>& out, std::string& error){
    std::vector<DrillNote> notes = readingPositionNotes(config);
    if (notes.size() < 2){
        error = "the position has fewer than two notes of the scale: widen the frets or the strings";
        return false;
    }
    // The rhythm first, then a note for each of its onsets
    RhythmConfig rhythm;
    rhythm.cells = config.cells;
    rhythm.bars = config.bars;
    rhythm.beatsPerBar = config.beatsPerBar;
    rhythm.tuning = config.tuning;
    out = buildRhythm(rhythm, rng);

    // A walk through the position's notes: a step or a small leap each time, turning back at the edges, never
    // standing still (a repeated note reads as a rhythm exercise, not a melody)
    const int count = (int)notes.size(), leap = std::max(1, std::min(config.maxLeap, count - 1));
    std::uniform_int_distribution<int> start(0, count - 1), move(1, leap), direction(0, 1);
    int at = start(rng);
    for (DrillNote& note : out){
        double beat = note.beat;
        note = notes[at];
        note.beat = beat;
        int step = move(rng) * (direction(rng) ? 1 : -1);
        if (at + step < 0 || at + step >= count) step = -step; // off an edge: the other way
        at = std::clamp(at + step, 0, count - 1);
    }
    return true;
}
