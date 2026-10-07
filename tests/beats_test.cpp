#include "doctest/doctest.h"

#include "core/backing.h"
#include "core/beats.h"

#include <cmath>
#include <vector>

// A song to listen to: a bass and a guitar part over `bars` bars of 4/4, played on hardthz's synths with its click
// (core/backing), after `lead` seconds of silence. The tempo is the chart's: one, or changing.
static std::vector<float> songOf(Chart& chart, int bars, double lead, int rate){
    chart.resolution = 480;
    chart.offset = 0.0;
    chart.timeSignatures = { { 0, 4, 4 } };
    chart.keys = { { 0, KeySignature{} } };
    chart.endTick = bars * 4 * 480;
    FrettedTrack bass, guitar;
    bass.type = InstrumentType::Bass;
    bass.tuning = { 28, 33, 38, 43 };
    guitar.tuning = { 40, 45, 50, 55, 59, 64 };
    const int roots[] = { 0, 5, 3, 7 }; // frets on the low string, bar by bar: E, A, G, B
    for (int bar = 0; bar < bars; bar++){
        int at = bar * 4 * 480, fret = roots[bar % 4];
        bass.notes.push_back({ at, 0, fret, 900 });              // the root on one, held
        bass.notes.push_back({ at + 960, 0, fret, 400 });        // and again on three
        bass.notes.push_back({ at + 1680, 0, fret, 200 });       // and on the "and" of four
        guitar.notes.push_back({ at + 480, 1, fret + 2, 400 });  // its fifth on two and four, an octave up
        guitar.notes.push_back({ at + 1440, 1, fret + 2, 400 });
    }
    chart.frettedTracks = { bass, guitar };
    std::vector<float> song = renderBacking(chart, rate);
    song.insert(song.begin(), (size_t)(lead * rate), 0.0f);
    return song;
}

// How far a time is from the nearest of some moments spaced `period` apart, one of them at `anchor`
static double offGrid(double time, double anchor, double period){
    double beats = (time - anchor) / period;
    return std::fabs(beats - std::round(beats)) * period;
}

TEST_CASE("a song's tempo and beats are found, and a chart's bars laid on them"){
    const int rate = 22050;
    const std::atomic<bool> cancel{false};
    Chart truth{};
    truth.tempoMap = { { 0, 96.0 } };
    const double lead = 0.37, beat = 60.0 / 96.0;
    std::vector<float> song = songOf(truth, 16, lead, rate);

    SongBeats found;
    REQUIRE(findSongBeats(song, rate, 4, found, cancel));
    REQUIRE(found.beats.size() > 60);
    CHECK(found.beats.front() < beat);                           // from the song's start...
    CHECK(found.beats.back() > song.size() / (double)rate - 2 * beat); // ...to its end
    for (double time : found.beats) CHECK(offGrid(time, lead, beat) < 0.02); // every one on a beat of the song

    Chart chart{};
    chart.resolution = 480;
    fitChartToBeats(chart, found.beats, found.downbeat);
    REQUIRE(chart.tempoMap.size() == 1);                         // one tempo: it's steady
    CHECK(chart.tempoMap[0].bpm == doctest::Approx(96.0).epsilon(0.001));
    CHECK(offGrid(chart.offset, lead, 4 * beat) < 0.02);         // bar 1 on a bar line of the song, not just on a beat
    // Laid on another beat, when the guess of the bar's first one is wrong: the same tempo, a beat later
    Chart later{};
    later.resolution = 480;
    fitChartToBeats(later, found.beats, found.downbeat + 1);
    CHECK(later.offset == doctest::Approx(chart.offset + beat).epsilon(0.001));
    CHECK(later.tempoMap[0].bpm == doctest::Approx(96.0).epsilon(0.001));
}

TEST_CASE("a song made to a click comes out at its round tempo exactly"){
    const int rate = 22050;
    const std::atomic<bool> cancel{false};
    Chart truth{};
    truth.tempoMap = { { 0, 120.0 } };
    std::vector<float> song = songOf(truth, 20, 1.0, rate);
    SongBeats found;
    REQUIRE(findSongBeats(song, rate, 4, found, cancel));
    Chart chart{};
    chart.resolution = 480;
    fitChartToBeats(chart, found.beats, found.downbeat);
    REQUIRE(chart.tempoMap.size() == 1);
    CHECK(chart.tempoMap[0].bpm == 120.0);
    CHECK(offGrid(chart.offset, 1.0, 0.5) < 0.015);
}

TEST_CASE("a band that speeds up: the bars follow it, beat by beat"){
    const int rate = 22050;
    const std::atomic<bool> cancel{false};
    Chart truth{};
    for (int bar = 0; bar < 16; bar++) truth.tempoMap.push_back({ bar * 4 * 480, 88.0 + bar }); // 88 to 103, a step each bar
    std::vector<float> song = songOf(truth, 16, 0.5, rate);
    SongBeats found;
    REQUIRE(findSongBeats(song, rate, 4, found, cancel));
    Chart chart{};
    chart.resolution = 480;
    fitChartToBeats(chart, found.beats, found.downbeat);
    CHECK(chart.tempoMap.size() > 6);
    // Slower at the start than at the end, near what it is (a drifting tempo is least sure at the song's two ends)
    CHECK(chart.tempoMap.front().bpm == doctest::Approx(88.0).epsilon(0.06));
    CHECK(chart.tempoMap.back().bpm == doctest::Approx(103.0).epsilon(0.06));
    // The chart's beats against the song's own, all the way: each of the chart's falls on one of the song's
    int close = 0, beats = 0;
    for (int tick = 0; tick < 15 * 4 * 480; tick += 480, beats++){
        double chartTime = tickToSeconds(chart, tick);
        double nearest = 1e9;
        for (int other = 0; other <= 16 * 4 * 480; other += 480) nearest = std::min(nearest, std::fabs(0.5 + tickToSeconds(truth, other) - chartTime));
        close += nearest < 0.03;
    }
    CAPTURE(close);
    CHECK(close >= beats * 9 / 10);
}

TEST_CASE("no beat is found in silence, a few seconds of sound, or a search that's stopped"){
    const std::atomic<bool> cancel{false};
    SongBeats found;
    CHECK_FALSE(findSongBeats(std::vector<float>(22050 * 10, 0.0f), 22050, 4, found, cancel));
    CHECK_FALSE(findSongBeats(std::vector<float>(22050 * 2, 0.1f), 22050, 4, found, cancel));
    Chart truth{};
    truth.tempoMap = { { 0, 100.0 } };
    std::vector<float> song = songOf(truth, 8, 0.0, 22050);
    const std::atomic<bool> stopped{true};
    CHECK_FALSE(findSongBeats(song, 22050, 4, found, stopped));
}
