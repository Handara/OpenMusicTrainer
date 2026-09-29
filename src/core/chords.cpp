#include "core/chords.h"

#include "core/music.h"

#include <cmath>
#include <numeric>

const int LOWEST_PITCH = 40;  // E2: a guitar's lowest note
const int HIGHEST_PITCH = 88; // E6: above that, a guitar's harmonics more than its notes
const float MIN_CHORD_SHARE = 0.5f; // at least half of the sound on the chord's notes
const float PI_F = 3.14159265f;

const std::vector<ChordInfo>& commonChords(){
    static const std::vector<ChordInfo> chords = {
        { "C",     0,  { 0, 4, 7 },     { -1, 3, 2, 0, 1, 0 } },
        { "Cmaj7", 0,  { 0, 4, 7, 11 }, { -1, 3, 2, 0, 0, 0 } },
        { "C7",    0,  { 0, 4, 7, 10 }, { -1, 3, 2, 3, 1, 0 } },
        { "D",     2,  { 0, 4, 7 },     { -1, -1, 0, 2, 3, 2 } },
        { "Dm",    2,  { 0, 3, 7 },     { -1, -1, 0, 2, 3, 1 } },
        { "D7",    2,  { 0, 4, 7, 10 }, { -1, -1, 0, 2, 1, 2 } },
        { "E",     4,  { 0, 4, 7 },     { 0, 2, 2, 1, 0, 0 } },
        { "Em",    4,  { 0, 3, 7 },     { 0, 2, 2, 0, 0, 0 } },
        { "E7",    4,  { 0, 4, 7, 10 }, { 0, 2, 0, 1, 0, 0 } },
        { "F",     5,  { 0, 4, 7 },     { 1, 3, 3, 2, 1, 1 } },
        { "Fmaj7", 5,  { 0, 4, 7, 11 }, { -1, -1, 3, 2, 1, 0 } },
        { "G",     7,  { 0, 4, 7 },     { 3, 2, 0, 0, 0, 3 } },
        { "G7",    7,  { 0, 4, 7, 10 }, { 3, 2, 0, 0, 0, 1 } },
        { "A",     9,  { 0, 4, 7 },     { -1, 0, 2, 2, 2, 0 } },
        { "Am",    9,  { 0, 3, 7 },     { -1, 0, 2, 2, 1, 0 } },
        { "A7",    9,  { 0, 4, 7, 10 }, { -1, 0, 2, 0, 2, 0 } },
        { "Am7",   9,  { 0, 3, 7, 10 }, { -1, 0, 2, 0, 1, 0 } },
        { "B7",    11, { 0, 4, 7, 10 }, { -1, 2, 1, 2, 0, 2 } },
    };
    return chords;
}

const ChordInfo* findChord(const std::string& name){
    for (const ChordInfo& chord : commonChords()) if (chord.name == name) return &chord;
    return nullptr;
}

std::array<float, 12> chroma(const float* samples, int count, int sampleRate){
    std::array<float, 12> notes{};
    if (count <= 0) return notes;
    // A Hann window first: it tapers the edges, so a note's energy doesn't leak into its neighbours
    std::vector<float> windowed(count);
    for (int i = 0; i < count; i++) windowed[i] = samples[i] * 0.5f * (1.0f - std::cos(2.0f * PI_F * i / (count - 1)));
    for (int pitch = LOWEST_PITCH; pitch <= HIGHEST_PITCH; pitch++){
        // Goertzel: the energy at one frequency, as a single tuned resonator run over the samples
        float coefficient = 2.0f * std::cos(2.0f * PI_F * midiToFrequency((float)pitch) / sampleRate);
        float previous = 0.0f, beforeThat = 0.0f;
        for (int i = 0; i < count; i++){
            float current = windowed[i] + coefficient * previous - beforeThat;
            beforeThat = previous;
            previous = current;
        }
        float energy = previous * previous + beforeThat * beforeThat - coefficient * previous * beforeThat;
        notes[pitch % 12] += std::sqrt(std::max(0.0f, energy)); // magnitudes: one loud note mustn't drown the rest
    }
    float total = std::accumulate(notes.begin(), notes.end(), 0.0f);
    if (total > 0.0f) for (float& share : notes) share /= total;
    return notes;
}

float chordFit(const std::array<float, 12>& notes, const ChordInfo& chord){
    float inside = 0.0f;
    for (int interval : chord.intervals) inside += notes[(chord.rootPitchClass + interval) % 12];
    return inside - (1.0f - inside); // a chord of more notes catches more by chance: what's outside counts against it
}

bool soundsLikeChord(const std::array<float, 12>& notes, const ChordInfo& chord){
    float fit = chordFit(notes, chord);
    if ((fit + 1.0f) / 2.0f < MIN_CHORD_SHARE) return false;
    for (int root = 0; root < 12; root++){
        for (const std::vector<int>& triad : { std::vector<int>{0, 4, 7}, std::vector<int>{0, 3, 7} }){
            ChordInfo other{ "", root, triad, {} };
            bool same = root == chord.rootPitchClass && triad == std::vector<int>(chord.intervals.begin(), chord.intervals.begin() + 3);
            if (!same && chordFit(notes, other) > fit + 1e-4f) return false;
        }
    }
    return true;
}
