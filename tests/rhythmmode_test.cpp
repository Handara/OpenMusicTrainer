#include "doctest/doctest.h"

#include "core/rhythmmode.h"

TEST_CASE("a part as rhythm: one hit a moment, low dons and high kas, chords big"){
    Chart chart{};
    chart.resolution = 480;
    chart.tempoMap = {{0, 120.0}};
    FrettedTrack guitar;
    guitar.tuning = { 40, 45, 50, 55, 59, 64 };
    guitar.notes = { {0, 0, 0, 0},                      // E2: low
                     {480, 5, 0, 0},                    // E4: high
                     {960, 0, 3, 0}, {960, 2, 2, 0},    // a chord: G2 and E3, its top E3
                     {1440, 4, 1, 0} };                 // C4
    chart.frettedTracks = {guitar};
    std::vector<RhythmHit> hits = rhythmHits(chart, 0);
    REQUIRE(hits.size() == 4);                          // the chord is one hit
    CHECK(hits[0].tick == 0);
    CHECK(hits[0].kind == RhythmHitKind::Don);
    CHECK(hits[1].kind == RhythmHitKind::Ka);
    CHECK(hits[2].big);
    CHECK_FALSE(hits[1].big);
    CHECK(hits[3].tick == 1440);
    // Tops 40, 64, 52, 60: the middle is 60, so 60 and 64 are kas, 40 and 52 dons
    CHECK(hits[2].kind == RhythmHitKind::Don);
    CHECK(hits[3].kind == RhythmHitKind::Ka);
}

TEST_CASE("keys parts become rhythm too, and an empty part has none"){
    Chart chart{};
    KeysTrack piano;
    piano.notes = { {0, 60, 0}, {0, 64, 0}, {0, 67, 0}, {480, 48, 0} };
    chart.keysTracks = {piano};
    std::vector<RhythmHit> hits = rhythmHits(chart, 0);
    REQUIRE(hits.size() == 2);
    CHECK(hits[0].big);
    CHECK(hits[0].kind == RhythmHitKind::Ka);  // the chord's top, 67, against the middle of 67 and 48
    CHECK(hits[1].kind == RhythmHitKind::Don);
    CHECK(rhythmHits(chart, 5).empty());
}
