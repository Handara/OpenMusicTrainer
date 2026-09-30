#include "core/chords.h"

#include "core/music.h"

#include <algorithm>
#include <cmath>
#include <numeric>

const int LOWEST_PITCH = 40;  // E2: a guitar's lowest note
const int HIGHEST_PITCH = 88; // E6: above that, a guitar's harmonics more than its notes
const float MIN_CHORD_SHARE = 0.5f; // at least half of the sound on the chord's notes
const float PI_F = 3.14159265f;
const float CHORD_NOTE_PEAK = 1.25f;   // a note held stands out this much against the half steps beside it
const int STRUM_HISTORY_FRAMES = 8;    // 80 ms of what came before
const float STRUM_JUMP = 2.5f;         // this much louder than it (about 8 dB)
const float STRUM_FLOOR = 0.01f;       // and above this level (-40 dB): not the noise of a quiet room
const double STRUM_GAP_S = 0.15;       // one strum per this long: a strum's own notes don't count again

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

// The chords nameChord knows: the notes above the root (in any octave), and the symbol's ending
struct ChordKind {
    std::vector<int> intervals;
    const char* suffix;
};
static const ChordKind CHORD_KINDS[] = {
    {{0, 4, 7}, ""},        {{0, 3, 7}, "m"},       {{0, 3, 6}, "dim"},     {{0, 4, 8}, "aug"},
    {{0, 2, 7}, "sus2"},    {{0, 5, 7}, "sus4"},    {{0, 7}, "5"},
    {{0, 4, 7, 10}, "7"},   {{0, 4, 7, 11}, "maj7"}, {{0, 3, 7, 10}, "m7"}, {{0, 3, 7, 11}, "mMaj7"},
    {{0, 3, 6, 10}, "m7b5"}, {{0, 3, 6, 9}, "dim7"}, {{0, 4, 7, 9}, "6"},   {{0, 3, 7, 9}, "m6"},
    {{0, 4, 7, 2}, "add9"}, {{0, 4, 7, 10, 2}, "9"}, {{0, 4, 7, 11, 2}, "maj9"}, {{0, 3, 7, 10, 2}, "m9"},
};

std::string nameChord(const std::vector<int>& pitches){
    if (pitches.empty()) return "";
    int bass = *std::min_element(pitches.begin(), pitches.end());
    unsigned held = 0; // the pitch classes held, one bit each
    for (int pitch : pitches) held |= 1u << (pitch % 12);
    // Every root the notes could be built on, the bass first: its reading wins a tie
    for (int step = 0; step < 12; step++){
        int root = (bass + step) % 12;
        if (!(held & (1u << root))) continue;
        for (const ChordKind& kind : CHORD_KINDS){
            unsigned wanted = 0;
            for (int interval : kind.intervals) wanted |= 1u << ((root + interval) % 12);
            if (wanted != held) continue;
            std::string name = std::string(pitchClassName(root)) + kind.suffix;
            if (root != bass % 12) name += std::string("/") + pitchClassName(bass);
            return name;
        }
    }
    return "";
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

// The energy at a frequency (Goertzel, over samples already windowed)
static float magnitudeAt(const std::vector<float>& windowed, int sampleRate, float frequency){
    float coefficient = 2.0f * std::cos(2.0f * PI_F * frequency / sampleRate);
    float previous = 0.0f, beforeThat = 0.0f;
    for (float sample : windowed){
        float current = sample + coefficient * previous - beforeThat;
        beforeThat = previous;
        previous = current;
    }
    return std::sqrt(std::max(0.0f, previous * previous + beforeThat * beforeThat - coefficient * previous * beforeThat));
}

// A note's harmonics together: a low string's fundamental is weak (a bass's pickups barely hear it), its harmonics
// aren't, and the higher ones are further from the neighbours' in hertz, so they tell notes apart in a short sound
static float harmonicStrength(const std::vector<float>& windowed, int sampleRate, float midi){
    const int HARMONICS = 8;
    const float highest = std::min(4000.0f, sampleRate * 0.45f);
    float frequency = midiToFrequency(midi), sum = 0.0f;
    for (int h = 1; h <= HARMONICS && frequency * h < highest; h++) sum += magnitudeAt(windowed, sampleRate, frequency * h);
    return sum;
}

bool soundHoldsNotes(const float* samples, int count, int sampleRate, const std::vector<int>& pitches){
    if (count <= 1 || pitches.empty()) return false;
    std::vector<float> windowed(count);
    for (int i = 0; i < count; i++) windowed[i] = samples[i] * 0.5f * (1.0f - std::cos(2.0f * PI_F * i / (count - 1)));
    float energy = 0.0f;
    for (float sample : windowed) energy += sample * sample;
    if (energy <= 1e-9f) return false; // silence holds nothing
    for (int pitch : pitches){
        float strength = harmonicStrength(windowed, sampleRate, (float)pitch);
        float below = harmonicStrength(windowed, sampleRate, pitch - 1.0f), above = harmonicStrength(windowed, sampleRate, pitch + 1.0f);
        if (strength < CHORD_NOTE_PEAK * std::max(below, above)) return false;
    }
    return true;
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

void initStrumDetector(StrumDetector& detector, int sampleRate){
    detector = StrumDetector{};
    detector.sampleRate = sampleRate;
    detector.frameSize = std::max(1, sampleRate / 100);
}

void feedStrumDetector(StrumDetector& detector, const float* samples, int count, std::vector<long long>& strums){
    for (int i = 0; i < count; i++){
        detector.frameSquares += samples[i] * samples[i];
        detector.position++;
        if (++detector.inFrame < detector.frameSize) continue;
        float level = std::sqrt(detector.frameSquares / detector.frameSize);
        long long frameStart = detector.position - detector.frameSize;
        detector.frameSquares = 0.0f;
        detector.inFrame = 0;

        std::vector<float>& history = detector.recentLevels;
        float before = history.empty() ? 0.0f : std::accumulate(history.begin(), history.end(), 0.0f) / history.size();
        bool rested = frameStart - detector.lastStrum >= (long long)(STRUM_GAP_S * detector.sampleRate);
        if (level > STRUM_FLOOR && level > STRUM_JUMP * before && rested){
            strums.push_back(frameStart);
            detector.lastStrum = frameStart;
        }
        history.push_back(level);
        if ((int)history.size() > STRUM_HISTORY_FRAMES) history.erase(history.begin());
    }
}
