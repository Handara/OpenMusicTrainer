#include "core/rhythm.h"

#include "core/notation.h"

#include <cstdlib>

const std::vector<RhythmCell>& rhythmCells(){
    static const std::vector<RhythmCell> cells = {
        { "quarter",        { 0.0 } },
        { "rest",           {} },
        { "eighths",        { 0.0, 0.5 } },
        { "offbeat",        { 0.5 } },                    // an eighth rest, then an eighth
        { "triplets",       { 0.0, 1.0 / 3, 2.0 / 3 } },
        { "sixteenths",     { 0.0, 0.25, 0.5, 0.75 } },
        { "gallop",         { 0.0, 0.5, 0.75 } },         // an eighth and two sixteenths
        { "reverse_gallop", { 0.0, 0.25, 0.5 } },         // two sixteenths and an eighth
        { "dotted",         { 0.0, 0.75 } },              // a dotted eighth and a sixteenth
    };
    return cells;
}

const RhythmCell* findRhythmCell(const std::string& name){
    for (const RhythmCell& cell : rhythmCells()) if (name == cell.name) return &cell;
    return nullptr;
}

int rhythmString(const std::vector<int>& tuning){
    int best = 0;
    for (int s = 0; s < (int)tuning.size(); s++){
        int distance = std::abs(staffNote(tuning[s] + WRITTEN_OCTAVE_SHIFT).position - STAFF_MIDDLE_LINE);
        if (distance < std::abs(staffNote(tuning[best] + WRITTEN_OCTAVE_SHIFT).position - STAFF_MIDDLE_LINE)) best = s;
    }
    return best;
}

std::vector<DrillNote> buildRhythm(const RhythmConfig& config, std::mt19937& rng){
    std::vector<const RhythmCell*> cells;
    for (const std::string& name : config.cells) if (const RhythmCell* cell = findRhythmCell(name)) cells.push_back(cell);
    if (cells.empty()) cells.push_back(findRhythmCell("quarter"));
    std::uniform_int_distribution<int> pick(0, (int)cells.size() - 1);

    std::vector<DrillNote> notes;
    const int string = rhythmString(config.tuning);
    const int pitch = config.tuning.empty() ? 59 : config.tuning[string];
    for (int bar = 0; bar < config.bars; bar++){
        // A bar of rests only would leave nothing to play: draw again until something sounds
        std::vector<const RhythmCell*> beats;
        bool sounds = false;
        while (!sounds){
            beats.clear();
            for (int beat = 0; beat < config.beatsPerBar; beat++){
                beats.push_back(cells[pick(rng)]);
                if (!beats.back()->onsets.empty()) sounds = true;
            }
        }
        for (int beat = 0; beat < config.beatsPerBar; beat++){
            for (double onset : beats[beat]->onsets){
                notes.push_back({ bar * config.beatsPerBar + beat + onset, string, 0, pitch });
            }
        }
    }
    return notes;
}
