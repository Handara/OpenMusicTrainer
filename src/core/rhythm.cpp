#include "core/rhythm.h"

#include "core/notation.h"

#include <cstdlib>

const std::vector<RhythmCell>& rhythmCells(){
    static const std::vector<RhythmCell> cells = {
        { "quarter",        { 0.0 } },
        { "rest",           {} },
        { "half",           { 0.0 }, 2 },
        { "whole",          { 0.0 }, 4 },
        { "dotted_half",    { 0.0 }, 3 },
        { "eighths",        { 0.0, 0.5 } },
        { "offbeat",        { 0.5 } },                    // an eighth rest, then an eighth
        { "dotted_quarter", { 0.0, 1.5 }, 2 },            // a dotted quarter and an eighth
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

    std::vector<DrillNote> notes;
    const int string = rhythmString(config.tuning);
    const int pitch = config.tuning.empty() ? 59 : config.tuning[string];
    // Where a cell longer than a beat may start: a half on an even beat, a whole or a dotted half on the downbeat
    auto fitsAt = [&](const RhythmCell& cell, int beat){
        if (beat + cell.beats > config.beatsPerBar) return false;
        if (cell.beats == 1) return true;
        if (cell.beats == 2) return beat % 2 == 0;
        return beat == 0;
    };
    for (int bar = 0; bar < config.bars; bar++){
        // A bar of rests only would leave nothing to play: draw again until something sounds
        std::vector<std::pair<int, const RhythmCell*>> placed; // each cell and the beat it starts on
        bool sounds = false;
        while (!sounds){
            placed.clear();
            for (int beat = 0; beat < config.beatsPerBar;){
                std::vector<const RhythmCell*> fitting;
                for (const RhythmCell* cell : cells) if (fitsAt(*cell, beat)) fitting.push_back(cell);
                const RhythmCell* cell = fitting.empty() ? findRhythmCell("quarter") : fitting[std::uniform_int_distribution<int>(0, (int)fitting.size() - 1)(rng)];
                placed.push_back({ beat, cell });
                if (!cell->onsets.empty()) sounds = true;
                beat += cell->beats;
            }
        }
        for (const auto& [beat, cell] : placed){
            for (size_t k = 0; k < cell->onsets.size(); k++){
                // It lasts until the cell's next note, or the cell's end (a rest after it is written as one)
                const double end = k + 1 < cell->onsets.size() ? cell->onsets[k + 1] : cell->beats;
                DrillNote note{ bar * config.beatsPerBar + beat + cell->onsets[k], string, 0, pitch };
                note.length = end - cell->onsets[k];
                notes.push_back(note);
            }
        }
    }
    return notes;
}
