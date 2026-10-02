#include "core/beats.h"

#include "core/fft.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <numeric>

const double SLOWEST_BPM = 60.0, FASTEST_BPM = 200.0;
const double LIKELY_BPM = 115.0;     // tempos near this are likelier, an octave either way much less
const double BEAT_TIGHTNESS = 100.0; // how much the beat resists speeding up or slowing down between two beats
const double STEADY_S = 0.015;       // beats within this of one steady row are on it: with nearly all of them on it
const double STEADY_SHARE = 0.9;     // (this share), the recording keeps one tempo. The few off it are beats found a
                                     // little wrong (at the song's ends, through a break), not the tempo moving.
const double ROUND_BPM = 0.08;       // a steady tempo this near a whole or half number is most likely that number...
const double ROUND_DRIFT_S = 0.02;   // ...if at that number no beat is further than this from where it was found
const float WINDOW_S = 0.023f;       // the sound looked at for each onset strength
const int FLUX_LAG = 4;              // strengths compare the sound with 20 ms before: a start takes about that long
const float LOW_HZ = 150.0f;         // under this: the kick drum and the bass, which bars lean on
const float HARMONY_S = 0.18f;       // the sound looked at in each beat for the notes it holds
const float HARMONY_LOW_HZ = 60.0f, HARMONY_HIGH_HZ = 1000.0f; // where the notes that make the harmony are

std::vector<double> trackBeats(std::vector<double> onsets){
    std::vector<double> beats;
    const int frames = (int)onsets.size();
    if (frames < 4 * BEAT_RATE) return beats; // under four seconds: no telling
    // On one scale, whatever the music: around 0, spread 1
    double mean = std::accumulate(onsets.begin(), onsets.end(), 0.0) / frames, spread = 0.0;
    for (double& o : onsets){ o -= mean; spread += o * o; }
    spread = std::sqrt(spread / frames);
    if (spread <= 0.0) return beats;
    for (double& o : onsets) o /= spread;

    // The tempo: the lag the onsets repeat at most, tempos near a common one favored
    const int shortest = (int)(60.0 / FASTEST_BPM * BEAT_RATE), longest = (int)(60.0 / SLOWEST_BPM * BEAT_RATE);
    std::vector<double> scores(longest + 2, 0.0);
    for (int lag = shortest; lag <= longest + 1; lag++){
        double sum = 0.0;
        for (int f = lag; f < frames; f++) sum += onsets[f] * onsets[f - lag];
        double bpm = 60.0 * BEAT_RATE / lag, octaves = std::log2(bpm / LIKELY_BPM);
        scores[lag] = sum * std::exp(-0.5 * octaves * octaves / (0.9 * 0.9));
    }
    int best = shortest;
    for (int lag = shortest; lag <= longest; lag++) if (scores[lag] > scores[best]) best = lag;
    double period = best;
    if (best > shortest && best < longest){ // between frames, from the scores either side
        double a = scores[best - 1], b = scores[best], c = scores[best + 1], bend = a - 2 * b + c;
        if (bend < 0.0) period += 0.5 * (a - c) / bend;
    }

    // The beats, by dynamic programming (Ellis's beat tracker): each frame's best score as a beat is its onset plus
    // the best beat a period or so before it, a period that strays from the tempo costing more the further it strays
    std::vector<double> score(frames, 0.0);
    std::vector<int> previous(frames, -1);
    for (int f = 0; f < frames; f++){
        int from = std::max(0, f - (int)std::lround(2.0 * period)), to = f - (int)std::lround(period / 2.0);
        double bestScore = 0.0;
        int bestFrame = -1;
        for (int p = from; p <= to; p++){
            double stray = std::log((double)(f - p) / period);
            double candidate = score[p] - BEAT_TIGHTNESS * stray * stray;
            if (bestFrame < 0 || candidate > bestScore){ bestScore = candidate; bestFrame = p; }
        }
        score[f] = onsets[f] + (bestFrame >= 0 ? bestScore : 0.0);
        previous[f] = bestFrame;
    }
    // The last beat: the best in the last period; the rest, back from it
    int last = std::max(0, frames - 1 - (int)period);
    for (int f = last; f < frames; f++) if (score[f] > score[last]) last = f;
    for (int f = last; f >= 0; f = previous[f]) beats.push_back((double)f / BEAT_RATE);
    std::reverse(beats.begin(), beats.end());
    if (beats.size() < 2) return {};

    // Smoothed: found on frames, each beat is a few milliseconds off, which reads as a tempo wobbling. A recording
    // that keeps one tempo (most do: a click track) gets that tempo exactly, the line through all its beats; one
    // that drifts, each beat evened out with the few either side of it.
    auto lineThrough = [&](size_t from, size_t to, double& start, double& slope){
        double n = (double)(to - from), meanIndex = 0.0, meanTime = 0.0;
        for (size_t i = from; i < to; i++){ meanIndex += (double)i; meanTime += beats[i]; }
        meanIndex /= n; meanTime /= n;
        double covariance = 0.0, variance = 0.0;
        for (size_t i = from; i < to; i++){ covariance += (i - meanIndex) * (beats[i] - meanTime); variance += (i - meanIndex) * (i - meanIndex); }
        slope = variance > 0.0 ? covariance / variance : 0.0;
        start = meanTime - slope * meanIndex;
    };
    auto off = [&](size_t i, double start, double slope){ return std::fabs(beats[i] - (start + slope * i)); };
    // The row through the beats that sit on it: fitted, then fitted again without the ones that didn't
    auto lineThroughMost = [&](double& start, double& slope){
        std::vector<char> on(beats.size(), 1);
        size_t count = beats.size();
        for (int pass = 0; pass < 3 && count >= 2; pass++){
            double n = 0.0, meanIndex = 0.0, meanTime = 0.0;
            for (size_t i = 0; i < beats.size(); i++) if (on[i]){ n++; meanIndex += (double)i; meanTime += beats[i]; }
            meanIndex /= n; meanTime /= n;
            double covariance = 0.0, variance = 0.0;
            for (size_t i = 0; i < beats.size(); i++) if (on[i]){ covariance += (i - meanIndex) * (beats[i] - meanTime); variance += (i - meanIndex) * (i - meanIndex); }
            slope = variance > 0.0 ? covariance / variance : 0.0;
            start = meanTime - slope * meanIndex;
            count = 0;
            for (size_t i = 0; i < beats.size(); i++){ on[i] = off(i, start, slope) < STEADY_S; count += on[i]; }
        }
        return (double)count / beats.size();
    };
    double start = 0.0, slope = 0.0;
    const bool steady = lineThroughMost(start, slope) >= STEADY_SHARE && slope > 0.0;
    if (steady){
        // Music made to a click is at a round tempo: 120, not 119.97. The round one is taken when the beats found
        // sit on it as well, all the way through.
        double bpm = 60.0 / slope, round = std::round(bpm * 2.0) / 2.0;
        if (std::fabs(bpm - round) < ROUND_BPM){
            double roundSlope = 60.0 / round, roundStart = 0.0, counted = 0.0;
            for (size_t i = 0; i < beats.size(); i++) if (off(i, start, slope) < STEADY_S){ roundStart += beats[i] - roundSlope * i; counted++; }
            roundStart /= std::max(1.0, counted);
            size_t onIt = 0;
            for (size_t i = 0; i < beats.size(); i++) onIt += off(i, roundStart, roundSlope) < ROUND_DRIFT_S;
            if ((double)onIt / beats.size() >= STEADY_SHARE){ start = roundStart; slope = roundSlope; }
        }
    }
    std::vector<double> smoothed(beats.size());
    for (size_t i = 0; i < beats.size(); i++){
        if (steady){
            smoothed[i] = start + slope * i;
        } else {
            size_t from = i >= 4 ? i - 4 : 0, to = std::min(beats.size(), i + 5);
            double localStart, localSlope;
            lineThrough(from, to, localStart, localSlope);
            smoothed[i] = localStart + localSlope * i;
        }
    }
    return smoothed;
}

// How much starts at each moment of a sound, BEAT_RATE times a second, over all of it (`all`) and in its low end
// (`low`). A start is energy that wasn't there a moment before: each moment's spectrum is compared with the one
// 20 ms earlier, and what rose is added up (what fell isn't a start). Loudness is counted as the ear does, by ratios.
static bool onsetStrengths(const std::vector<float>& samples, int sampleRate, std::vector<double>& all, std::vector<double>& low,
                           const std::atomic<bool>& cancel){
    int size = 64;
    while (size < WINDOW_S * sampleRate) size <<= 1;
    const int frames = (int)((double)samples.size() / sampleRate * BEAT_RATE);
    const int bins = size / 2, lowBins = std::max(2, (int)(LOW_HZ * size / sampleRate) + 1);
    if (frames <= FLUX_LAG) return false;
    const std::vector<std::complex<float>> twiddles = fftTwiddles(size);
    std::vector<float> window(size);
    for (int i = 0; i < size; i++) window[i] = 0.5f - 0.5f * std::cos(2.0f * 3.14159265f * i / (size - 1));
    std::vector<std::complex<float>> spectrum(size);
    std::vector<std::vector<float>> recent(FLUX_LAG + 1, std::vector<float>(bins, 0.0f)); // the last few spectra, in a ring
    all.assign(frames, 0.0);
    low.assign(frames, 0.0);
    for (int f = 0; f < frames; f++){
        if (f % 512 == 0 && cancel) return false;
        // The window is centered on the moment
        const long long center = (long long)((double)f * sampleRate / BEAT_RATE);
        for (int i = 0; i < size; i++){
            long long at = center - size / 2 + i;
            spectrum[i] = at >= 0 && at < (long long)samples.size() ? samples[(size_t)at] * window[i] : 0.0f;
        }
        fft(spectrum, twiddles, false);
        std::vector<float>& now = recent[f % (FLUX_LAG + 1)];
        const std::vector<float>& before = recent[(f + 1) % (FLUX_LAG + 1)]; // the oldest kept: FLUX_LAG moments ago
        for (int k = 1; k < bins; k++){
            now[k] = std::log1p(1000.0f * std::abs(spectrum[k]) / size);
            float rise = now[k] - before[k];
            if (f >= FLUX_LAG && rise > 0.0f){
                all[f] += rise;
                if (k < lowBins) low[f] += rise;
            }
        }
    }
    return true;
}

// The notes sounding in each beat: the 12 note names' shares of its sound (its chroma), from the middle of the
// beat, past its attacks, where the notes ring
static std::vector<std::array<double, 12>> beatHarmonies(const std::vector<float>& samples, int sampleRate, const std::vector<double>& beats){
    int size = 256;
    while (size < HARMONY_S * sampleRate) size <<= 1;
    const std::vector<std::complex<float>> twiddles = fftTwiddles(size);
    std::vector<std::complex<float>> spectrum(size);
    std::vector<std::array<double, 12>> harmonies(beats.size(), std::array<double, 12>{});
    for (size_t i = 0; i + 1 < beats.size(); i++){
        const long long start = (long long)((beats[i] + beats[i + 1]) / 2 * sampleRate) - size / 2;
        for (int k = 0; k < size; k++){
            long long at = start + k;
            float window = 0.5f - 0.5f * std::cos(2.0f * 3.14159265f * k / (size - 1));
            spectrum[k] = at >= 0 && at < (long long)samples.size() ? samples[(size_t)at] * window : 0.0f;
        }
        fft(spectrum, twiddles, false);
        std::array<double, 12>& notes = harmonies[i];
        double total = 0.0;
        for (int k = 1; k < size / 2; k++){
            double hz = (double)k * sampleRate / size;
            if (hz < HARMONY_LOW_HZ || hz > HARMONY_HIGH_HZ) continue;
            int note = (int)std::lround(12.0 * std::log2(hz / 440.0)) + 120 * 12; // semitones from A, kept positive
            notes[note % 12] += std::abs(spectrum[k]);
            total += std::abs(spectrum[k]);
        }
        if (total > 0.0) for (double& share : notes) share /= total;
    }
    return harmonies;
}

// How alike two beats' (or bars') notes are: 1 for the same, 0 for none in common
static double alike(const std::array<double, 12>& a, const std::array<double, 12>& b){
    double dot = 0.0, lengthA = 0.0, lengthB = 0.0;
    for (int n = 0; n < 12; n++){ dot += a[n] * b[n]; lengthA += a[n] * a[n]; lengthB += b[n] * b[n]; }
    return lengthA > 0.0 && lengthB > 0.0 ? dot / std::sqrt(lengthA * lengthB) : 0.0;
}

// A clue's say for each place in the bar, from 0 (its least) to 1 (its most); nothing when it barely tells them apart
static void asShares(std::vector<double>& clue){
    const double most = *std::max_element(clue.begin(), clue.end()), least = *std::min_element(clue.begin(), clue.end());
    for (double& value : clue) value = most - least > 0.05 * std::fabs(most) ? (value - least) / (most - least) : 0.0;
}

bool findSongBeats(const std::vector<float>& samples, int sampleRate, int beatsPerBar, SongBeats& out, const std::atomic<bool>& cancel){
    out = SongBeats{};
    std::vector<double> all, low;
    if (sampleRate <= 0 || !onsetStrengths(samples, sampleRate, all, low, cancel)) return false;
    std::vector<double> beats = trackBeats(all);
    if (beats.size() < 8) return false;
    // Carried back to the song's start and on to its end at the tempo there, so every moment of it is in a bar
    const double length = (double)samples.size() / sampleRate;
    while (beats.front() - (beats[1] - beats[0]) >= 0.0) beats.insert(beats.begin(), beats.front() - (beats[1] - beats[0]));
    while (beats.back() + (beats.back() - beats[beats.size() - 2]) <= length) beats.push_back(beats.back() + (beats.back() - beats[beats.size() - 2]));

    // The first beat of the bar, of the places a beat can have in one. Two clues, each counting as much as the
    // other: where the low end starts hardest (the kick, the bass), and where bars cut the music so that each holds
    // one harmony (chords change on bar lines: a bar cut elsewhere straddles two, and its beats are less alike).
    if (cancel) return false;
    beatsPerBar = std::max(1, beatsPerBar);
    const std::vector<std::array<double, 12>> harmonies = beatHarmonies(samples, sampleRate, beats);
    std::vector<double> lowEnd(beatsPerBar, 0.0), oneHarmony(beatsPerBar, 0.0);
    for (size_t i = 0; i < beats.size(); i++){
        int frame = (int)std::lround(beats[i] * BEAT_RATE);
        double strongest = 0.0;
        for (int f = std::max(0, frame - 3); f <= std::min((int)low.size() - 1, frame + 3); f++) strongest = std::max(strongest, low[f]);
        lowEnd[i % beatsPerBar] += strongest;
    }
    for (int place = 0; place < beatsPerBar; place++){
        for (size_t bar = place; bar + beatsPerBar <= beats.size(); bar += beatsPerBar){
            std::array<double, 12> whole{};
            for (int b = 0; b < beatsPerBar; b++) for (int n = 0; n < 12; n++) whole[n] += harmonies[bar + b][n];
            for (int b = 0; b < beatsPerBar; b++) oneHarmony[place] += alike(harmonies[bar + b], whole);
        }
    }
    asShares(lowEnd);
    asShares(oneHarmony);
    int downbeat = 0;
    for (int place = 1; place < beatsPerBar; place++){
        if (lowEnd[place] + oneHarmony[place] > lowEnd[downbeat] + oneHarmony[downbeat]) downbeat = place;
    }
    out.downbeat = downbeat;
    out.beats = beats;
    return true;
}

void fitChartToBeats(Chart& chart, const std::vector<double>& beats, int first){
    if (beats.size() < 2 || chart.resolution <= 0) return;
    first = std::clamp(first, 0, (int)beats.size() - 2);
    chart.offset = std::round(beats[first] * 10000.0) / 10000.0;
    chart.tempoMap.clear();
    for (size_t i = (size_t)first; i + 1 < beats.size(); i++){
        double bpm = 60.0 / (beats[i + 1] - beats[i]);
        if (!chart.tempoMap.empty() && std::fabs(chart.tempoMap.back().bpm - bpm) < 0.01) continue;
        chart.tempoMap.push_back({ (int)(i - first) * chart.resolution, bpm });
    }
    // A steady tempo is written as the number it is: 120, not 119.99999999
    for (TempoChange& tempo : chart.tempoMap) tempo.bpm = std::round(tempo.bpm * 1000.0) / 1000.0;
}
