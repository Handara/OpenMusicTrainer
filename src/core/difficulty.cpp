#include "core/difficulty.h"

#include <algorithm>
#include <cmath>
#include <map>

const double STRAIN_DECAY = 0.3;   // what's left of the strain after a second without a note
const double STRAIN_SCALE = 0.35;  // a note's share of strain
const double SECTION_S = 0.4;      // the part is cut into sections this long; each keeps its peak strain
const double SECTION_WEIGHT = 0.9; // the sections' peaks, hardest first, each counts this much of the one before
const double SHORTEST_GAP_S = 0.04; // notes closer than this (a strum's) count as this far apart
const float STAR_SCALE = 1.25f;
const float PP_SCALE = 10.0f;
const float TOTAL_WEIGHT = 0.95f;

namespace {

// One moment notes start together: when, how many, where the hand is (frets; a piano's keys count half a fret
// each, an octave's jump about six frets' move) and how far it stretches
struct Onset {
    double time = 0.0;
    int count = 0;
    float position = 0.0f;
    float row = 0.0f;     // the strings (a piano: none)
    float stretch = 0.0f;
    bool open = false;    // open strings only: the fretting hand stays where it is
};

std::vector<Onset> onsetsOf(const Chart& chart, int part){
    std::map<int, std::vector<std::pair<float, float>>> byTick; // tick: (position, row) of each note
    const int fretted = (int)chart.frettedTracks.size();
    const bool frettedPart = part >= 0 && part < fretted;
    if (frettedPart){
        for (const FrettedNote& note : chart.frettedTracks[(size_t)part].notes) byTick[note.tick].push_back({ (float)note.fret, (float)note.stringIndex });
    } else if (part >= fretted && part < fretted + (int)chart.keysTracks.size()){
        for (const KeysNote& note : chart.keysTracks[(size_t)(part - fretted)].notes) byTick[note.tick].push_back({ note.pitch * 0.5f, 0.0f });
    }
    std::vector<Onset> onsets;
    for (const auto& [tick, notes] : byTick){
        Onset onset;
        onset.time = tickToSeconds(chart, tick);
        onset.count = (int)notes.size();
        float low = 1e9f, high = -1e9f, rows = 0.0f;
        for (const auto& [position, row] : notes){
            // An open string asks nothing of the fretting hand
            if (!frettedPart || position > 0.0f){
                low = std::min(low, position);
                high = std::max(high, position);
            }
            rows += row;
        }
        onset.open = low > high;
        if (onset.open) low = high = 0.0f;
        onset.position = (low + high) / 2;
        onset.stretch = high - low;
        onset.row = rows / onset.count;
        onsets.push_back(onset);
    }
    return onsets;
}

} // namespace

float partStars(const Chart& chart, int part){
    const std::vector<Onset> onsets = onsetsOf(chart, part);
    if (onsets.empty()) return 0.0f;
    std::map<long long, double> peaks; // by section
    double strain = 0.0;
    float hand = -1.0f; // where the fretting hand is: the last fretted notes' place (-1 before any)
    for (size_t i = 0; i < onsets.size(); i++){
        const Onset& onset = onsets[i];
        const double gap = i == 0 ? 1.0 : std::max(SHORTEST_GAP_S, onset.time - onsets[i - 1].time);
        double move = i > 0 ? 0.5 * std::fabs(onset.row - onsets[i - 1].row) : 0.0; // across the strings
        if (!onset.open){
            if (hand >= 0.0f) move += std::fabs(onset.position - hand); // along the neck (a piano: along the keys)
            hand = onset.position;
        }
        const double added = (1.0 / gap) * (1.0 + 0.08 * std::min(move, 12.0)) * (1.0 + 0.2 * (onset.count - 1)) * (1.0 + 0.03 * onset.stretch);
        strain = strain * std::pow(STRAIN_DECAY, gap) + STRAIN_SCALE * added;
        double& peak = peaks[(long long)std::floor(onset.time / SECTION_S)];
        peak = std::max(peak, strain);
    }
    std::vector<double> sorted;
    for (const auto& [section, peak] : peaks) sorted.push_back(peak);
    std::sort(sorted.rbegin(), sorted.rend());
    double sum = 0.0, weights = 0.0, weight = 1.0;
    for (double peak : sorted){
        sum += peak * weight;
        weights += weight;
        weight *= SECTION_WEIGHT;
    }
    return STAR_SCALE * (float)std::sqrt(sum / weights);
}

float runPerformance(float stars, float accuracy, int misses, int notes, bool withInstrument){
    if (!withInstrument || stars <= 0.0f || notes <= 0) return 0.0f;
    const float right = std::clamp(accuracy / 100.0f, 0.0f, 1.0f);
    // Longer parts are worth a little more: holding it together for longer
    const float length = 0.95f + 0.4f * std::min(1.0f, notes / 2000.0f) + (notes > 2000 ? 0.5f * std::log10(notes / 2000.0f) : 0.0f);
    return PP_SCALE * stars * stars * std::pow(right, 5.0f) * std::pow(0.97f, (float)std::max(0, misses)) * length;
}

float performanceWeight(int place){
    return std::pow(TOTAL_WEIGHT, (float)std::max(0, place));
}

float totalPerformance(std::vector<float> bests){
    std::sort(bests.rbegin(), bests.rend());
    float total = 0.0f;
    for (size_t i = 0; i < bests.size(); i++) total += bests[i] * performanceWeight((int)i);
    return total;
}
