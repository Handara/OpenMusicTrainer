#include "doctest/doctest.h"

#include "core/take.h"

#include <cstdlib>

// A bass part at 120 beats a minute, 480 ticks to the beat: a beat is half a second, a sixteenth 120 ticks
static Chart emptyChart(){
    Chart chart{};
    chart.resolution = 480;
    chart.offset = 0.0;
    chart.tempoMap = { { 0, 120.0 } };
    chart.timeSignatures = { { 0, 4, 4 } };
    chart.endTick = 16 * 480;
    FrettedTrack bass;
    bass.type = InstrumentType::Bass;
    bass.tuning = { 28, 33, 38, 43 };
    chart.frettedTracks = { bass };
    return chart;
}

TEST_CASE("a take: each note on the grid's nearest step, held until the next"){
    Chart chart = emptyChart();
    FrettedTrack& bass = chart.frettedTracks[0];
    Take take;
    const int sixteenth = 120;
    takePluck(take, chart, bass, sixteenth, { 33 }, 0.51);  // a hair late for beat 2: A, open
    takePluck(take, chart, bass, sixteenth, { 36 }, 0.98);  // a hair early for beat 3: C, on the A string
    takePluck(take, chart, bass, sixteenth, { 38 }, 1.26);  // 1.25 s is tick 1200: D, open
    endTake(take, chart, bass, sixteenth, 2.0);
    REQUIRE(bass.notes.size() == 3);
    CHECK(bass.notes[0].tick == 480);
    CHECK(bass.notes[0].stringIndex == 1);
    CHECK(bass.notes[0].fret == 0);
    CHECK(bass.notes[0].duration == 480);  // to the next note
    CHECK(bass.notes[1].tick == 960);
    CHECK(bass.notes[1].stringIndex == 1); // near the hand: the A string's 3rd fret, not the E string's 8th
    CHECK(bass.notes[1].fret == 3);
    CHECK(bass.notes[1].duration == 240);
    CHECK(bass.notes[2].tick == 1200);
    CHECK(bass.notes[2].duration == 1920 - 1200); // to the take's end
    CHECK(take.written.size() == 3);
}

TEST_CASE("a take: a note ends where its sound dies away, and grows while it rings"){
    Chart chart = emptyChart();
    FrettedTrack& bass = chart.frettedTracks[0];
    Take take;
    takePluck(take, chart, bass, 120, { 28 }, 0.0);
    takeLevel(take, chart, bass, 120, -12.0f, 0.1);
    takeLevel(take, chart, bass, 120, -18.0f, 0.5);
    CHECK(bass.notes[0].duration == 480);  // still ringing: as long as it has rung
    takeLevel(take, chart, bass, 120, -40.0f, 0.74); // muted: 28 dB under its loudest
    CHECK(bass.notes[0].duration == 720);
    takeLevel(take, chart, bass, 120, -70.0f, 1.5);
    endTake(take, chart, bass, 120, 3.0);
    CHECK(bass.notes[0].duration == 720);  // it had ended
    // A note is never shorter than a step
    takePluck(take, chart, bass, 120, { 33 }, 3.0);
    takeLevel(take, chart, bass, 120, -80.0f, 3.01);
    CHECK(bass.notes[1].duration == 120);
}

TEST_CASE("a take: strings plucked together are written together, in place of the one note heard first"){
    Chart chart = emptyChart();
    FrettedTrack& bass = chart.frettedTracks[0];
    Take take;
    takePluck(take, chart, bass, 120, { 45 }, 0.5);          // A2
    takePluck(take, chart, bass, 120, { 28 }, 1.0);          // what the note detector made of the pluck: a muddle
    takePluck(take, chart, bass, 120, { 40, 47 }, 1.004);    // what it was: E2 and B2
    endTake(take, chart, bass, 120, 2.0);
    REQUIRE(bass.notes.size() == 3);
    CHECK(bass.notes[0].duration == 480);
    CHECK(bass.notes[1].tick == 960);
    CHECK(bass.notes[2].tick == 960);
    CHECK(bass.notes[1].stringIndex != bass.notes[2].stringIndex);
    CHECK(bass.tuning[bass.notes[1].stringIndex] + bass.notes[1].fret == 40);
    CHECK(bass.tuning[bass.notes[2].stringIndex] + bass.notes[2].fret == 47);
    CHECK(std::abs(bass.notes[1].fret - bass.notes[2].fret) <= 3); // one hand shape
    CHECK(take.written.size() == 3);
}

TEST_CASE("a take: over what's there already, and around what can't be written"){
    Chart chart = emptyChart();
    FrettedTrack& bass = chart.frettedTracks[0];
    bass.notes = { { 0, 0, 5, 0 }, { 480, 1, 7, 0 }, { 1440, 2, 0, 0 } };
    Take take;
    takePluck(take, chart, bass, 120, { 33 }, 0.5);    // on the A string at tick 480, where fret 7 was: now open
    takePluck(take, chart, bass, 120, { 20 }, 0.75);   // below the low E: not this bass's
    takePluck(take, chart, bass, 120, { 33 }, -0.4);   // before the song
    endTake(take, chart, bass, 120, 1.0);
    REQUIRE(bass.notes.size() == 3);
    CHECK(bass.notes[1].fret == 0);
    CHECK(bass.notes[1].duration == 480);
    CHECK(bass.notes[0].fret == 5);
    CHECK(take.written.size() == 1);
    // Triplets: the grid is whatever step is given
    takePluck(take, chart, bass, 160, { 40 }, 1.17);   // 1.1667 s is a triplet eighth after beat 3 (1120 ticks)
    CHECK(bass.notes[2].tick == 1120);
}
