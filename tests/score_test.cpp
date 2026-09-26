#include "doctest/doctest.h"

#include "core/score.h"

#include <string>

// A chart in memory: 120 bpm, 480 ticks per quarter, one guitar track with these (tick, duration) notes
static Chart chartWith(std::vector<std::pair<int, int>> notes, int bars = 1, TimeSignatureChange time = {0, 4, 4}){
    Chart chart{};
    chart.version = 2;
    chart.resolution = 480;
    chart.tempoMap = {{0, 120.0}};
    chart.timeSignatures = {time};
    chart.keys = {{0, KeySignature{}}};
    chart.endTick = bars * ticksPerBar(chart, time);
    FrettedTrack track;
    track.type = InstrumentType::Guitar;
    track.tuning = {40, 45, 50, 55, 59, 64};
    for (auto [tick, duration] : notes) track.notes.push_back({tick, 0, 0, duration});
    chart.frettedTracks = {track};
    return chart;
}

// The rhythm as text, one token per event: q = quarter, e. = dotted eighth, re = eighth rest, 3e = triplet eighth,
// ~ = tied to the next, R = a whole bar's rest, | = bar line
static std::string rhythm(const Chart& chart){
    Score score = buildScore(chart, chart.frettedTracks[0]);
    const char* names[] = {"w", "h", "q", "e", "s", "t"}; // whole, half, quarter, eighth, sixteenth, 32nd
    std::string text;
    int bar = 0;
    for (const ScoreEvent& event : score.events){
        if (event.bar != bar){ text += " |"; bar = event.bar; }
        if (!text.empty()) text += " ";
        if (event.wholeBarRest){ text += "R"; continue; }
        if (event.rest) text += "r";
        if (event.tuplet == 3) text += "3";
        text += names[(int)event.value];
        if (event.dots) text += ".";
        if (event.tiedToNext) text += "~";
    }
    return text;
}

TEST_CASE("plain values in 4/4"){
    CHECK(rhythm(chartWith({{0, 0}, {480, 0}, {960, 0}, {1440, 0}})) == "q q q q");
    CHECK(rhythm(chartWith({{0, 0}, {240, 0}, {480, 0}, {720, 0}, {960, 0}, {1440, 0}})) == "e e e e q q");
    CHECK(rhythm(chartWith({{0, 0}, {120, 0}, {240, 0}, {360, 0}, {480, 0}})) == "s s s s h.");
    CHECK(rhythm(chartWith({{0, 1920}})) == "w");
    CHECK(rhythm(chartWith({{0, 0}, {720, 0}, {960, 0}})) == "q. e h"); // dotted quarter, eighth
}

TEST_CASE("a note without a length rings until the next note or its bar line"){
    CHECK(rhythm(chartWith({{960, 0}})) == "rh h");            // rests before it, then to the bar line
    CHECK(rhythm(chartWith({{0, 0}}, 2)) == "w | R");           // the next bar is empty: a whole-bar rest
    CHECK(rhythm(chartWith({{0, 240}, {960, 0}})) == "e re rq h"); // a given length leaves rests after it
}

TEST_CASE("notes are split and tied to show the beats"){
    // Off the beat, a note can't cross into the next beat: the quarter from 240 becomes two tied eighths
    CHECK(rhythm(chartWith({{0, 0}, {240, 0}, {720, 0}, {960, 0}})) == "e e~ e e h");
    // A syncopation within one beat is fine as it is: sixteenth, eighth, sixteenth
    CHECK(rhythm(chartWith({{0, 0}, {120, 0}, {360, 0}, {480, 0}})) == "s e s h.");
    // Across a bar line: tied into the next bar
    CHECK(rhythm(chartWith({{1440, 960}}, 2)) == "rh rq q~ | q rq rh"); // its length ends it a beat into the bar
}

TEST_CASE("rests show the beats"){
    // A rest from beat 2 to the end of a 4/4 bar is a quarter rest then a half rest, not a half then a quarter
    CHECK(rhythm(chartWith({{0, 480}})) == "q rq rh");
    CHECK(rhythm(chartWith({{0, 240}})) == "e re rq rh");
    CHECK(rhythm(chartWith({{1440, 0}})) == "rh rq q");                   // a half rest on beat 1 is fine in 4/4
}

TEST_CASE("triplets"){
    // Three notes a beat: triplet eighths
    CHECK(rhythm(chartWith({{0, 0}, {160, 0}, {320, 0}, {480, 0}, {960, 0}})) == "3e 3e 3e q h");
    // A triplet quarter then eighth, and six to a beat
    CHECK(rhythm(chartWith({{0, 0}, {320, 0}, {480, 0}, {560, 0}, {640, 0}, {720, 0}, {800, 0}, {880, 0}, {960, 0}}))
          == "3q 3e 3s 3s 3s 3s 3s 3s h");
}

TEST_CASE("compound and other meters"){
    TimeSignatureChange sixEight = {0, 6, 8};
    CHECK(rhythm(chartWith({{0, 0}, {720, 0}}, 1, sixEight)) == "q. q.");
    CHECK(rhythm(chartWith({{0, 0}, {240, 0}, {480, 0}, {720, 0}}, 1, sixEight)) == "e e e q.");
    CHECK(rhythm(chartWith({{0, 0}, {240, 0}}, 1, sixEight)) == "e q~ q."); // split at the second beat
    CHECK(rhythm(chartWith({{0, 1440}}, 1, {0, 3, 4})) == "h.");
    CHECK(rhythm(chartWith({{960, 0}}, 1, {0, 3, 4})) == "rq rq q"); // two quarter rests: no half rests in 3/4
    CHECK(rhythm(chartWith({{0, 0}, {480, 0}}, 1, {0, 2, 2})) == "q q~ h"); // cut time: split at the half-note beat
}

TEST_CASE("beams join eighths and shorter within a beat"){
    Chart chart = chartWith({{0, 0}, {240, 0}, {480, 0}, {720, 0}, {960, 0}, {1200, 0}, {1440, 0}});
    Score score = buildScore(chart, chart.frettedTracks[0]);
    REQUIRE(score.events.size() == 7);
    CHECK(score.events[0].beamGroup >= 0);
    CHECK(score.events[0].beamGroup == score.events[1].beamGroup);
    CHECK(score.events[2].beamGroup == score.events[3].beamGroup);
    CHECK(score.events[1].beamGroup != score.events[2].beamGroup); // a new beat, a new beam
    CHECK(score.events[6].beamGroup == -1);                        // the quarter isn't beamed

    // A lone eighth next to a rest keeps its flag
    Chart lone = chartWith({{0, 240}, {480, 0}});
    Score loneScore = buildScore(lone, lone.frettedTracks[0]);
    CHECK(loneScore.events[0].beamGroup == -1);

    // Compound meters beam in threes
    Chart sixEight = chartWith({{0, 0}, {240, 0}, {480, 0}, {720, 0}, {960, 0}, {1200, 0}}, 1, {0, 6, 8});
    Score compound = buildScore(sixEight, sixEight.frettedTracks[0]);
    CHECK(compound.events[0].beamGroup == compound.events[2].beamGroup);
    CHECK(compound.events[2].beamGroup != compound.events[3].beamGroup);
}

TEST_CASE("chords, times and bars"){
    Chart chart = chartWith({{0, 0}, {960, 0}}, 2);
    chart.frettedTracks[0].notes.insert(chart.frettedTracks[0].notes.begin() + 1, FrettedNote{0, 2, 2, 0}); // a chord at 0
    chart.keys = {{0, {1, false}}, {1920, {-1, false}}};
    Score score = buildScore(chart, chart.frettedTracks[0]);
    CHECK(score.events[0].firstNote == 0);
    CHECK(score.events[0].noteCount == 2);
    CHECK(score.events[1].firstNote == 2);
    CHECK(score.events[1].time == doctest::Approx(1.0f)); // tick 960 at 120 bpm
    REQUIRE(score.bars.size() == 3);                       // two bars and the closing line
    CHECK(score.bars[1].time == doctest::Approx(2.0f));
    CHECK(score.bars[0].showKey);
    CHECK(score.bars[0].showTimeSignature);
    CHECK(score.bars[1].showKey);                           // G major to F major
    CHECK(score.bars[1].key.fifths == -1);
    CHECK_FALSE(score.bars[1].showTimeSignature);
}
