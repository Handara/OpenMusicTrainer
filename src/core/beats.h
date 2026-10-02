#pragma once

#include "core/chart.h"

#include <atomic>
#include <vector>

// Finding the beat of music, and laying a chart's bars on it. Nobody gives the game a click: all it has is where
// sounds start. Those starts come round at the beat's period, which gives the tempo; then the beats themselves are
// the row of moments, about a period apart, that falls on the most starts (Ellis's beat tracker). A recording that
// keeps one tempo gets that tempo exactly; one that drifts gets its beats one by one. Pure math.
//
// It's used on a bass line's notes (core/transcribe) and on a whole song (findSongBeats, for the song editor).

const int BEAT_RATE = 200; // onset strengths a second: one every 5 ms

// The beats under a curve of onset strengths (how much starts at each moment, BEAT_RATE values a second, on any
// scale): when each falls, in seconds. Empty when there's too little to go on.
std::vector<double> trackBeats(std::vector<double> onsets);

// A song's beats, from its sound (mono), with the first beat of a bar picked out: `downbeat` is one of them (its
// number in `beats`), every `beatsPerBar`-th from it another. The beats cover the whole song, from its start.
// The downbeat is the surest guess there is (the beat the low end leans on), and still a guess: a chart can be laid
// on any other beat instead (fitChartToBeats). `cancel` stops it early. False for a sound with no beat to find.
struct SongBeats {
    std::vector<double> beats;
    int downbeat = 0;
};
bool findSongBeats(const std::vector<float>& samples, int sampleRate, int beatsPerBar, SongBeats& out, const std::atomic<bool>& cancel);

// A chart's bars laid on beats: tick 0 on the beat numbered `first`, a beat of the chart for each beat after it, the
// tempo following them (one tempo for a steady row, a change wherever the gap between two beats changes). The
// chart's offset becomes where that first beat falls. Notes keep their ticks: their bars and beats.
void fitChartToBeats(Chart& chart, const std::vector<double>& beats, int first);
