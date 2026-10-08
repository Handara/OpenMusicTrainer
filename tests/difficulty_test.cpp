#include "doctest/doctest.h"

#include "core/difficulty.h"

#include <functional>

// A guitar part: `count` notes, `perBeat` to a beat at `bpm`, each at the place given for it
static Chart guitarPart(int bpm, int perBeat, int count, const std::function<std::pair<int, int>(int)>& place){
    Chart chart{};
    chart.resolution = 480;
    chart.tempoMap = {{0, (double)bpm}};
    FrettedTrack track;
    track.type = InstrumentType::Guitar;
    track.tuning = { 40, 45, 50, 55, 59, 64 };
    for (int i = 0; i < count; i++){
        const auto [string, fret] = place(i);
        track.notes.push_back({ i * 480 / perBeat, string, fret, 0 });
    }
    chart.frettedTracks = { track };
    return chart;
}

TEST_CASE("stars: quick, moving and chorded parts are harder"){
    const float quarters = partStars(guitarPart(90, 1, 64, [](int){ return std::make_pair(5, 0); }), 0);
    const float eighths = partStars(guitarPart(120, 2, 128, [](int i){ return std::make_pair(3 + i % 3, (i * 2) % 5); }), 0);
    const float sixteenths = partStars(guitarPart(140, 4, 256, [](int i){ return std::make_pair(i % 6, (i * 7) % 15); }), 0);
    MESSAGE("quarters " << quarters << ", eighths " << eighths << ", sixteenths " << sixteenths);
    // Where the scale sits: about 1, about 3, 6 and up
    CHECK(quarters == doctest::Approx(1.2f).epsilon(0.25));
    CHECK(eighths > 2.3f);
    CHECK(eighths < 4.0f);
    CHECK(sixteenths > 6.0f);

    // The same notes faster are harder; the same rhythm across the neck is harder than on one fret
    CHECK(partStars(guitarPart(160, 1, 64, [](int){ return std::make_pair(5, 0); }), 0) > quarters);
    const float still = partStars(guitarPart(120, 2, 64, [](int){ return std::make_pair(2, 5); }), 0);
    const float jumping = partStars(guitarPart(120, 2, 64, [](int i){ return std::make_pair(2, i % 2 ? 2 : 12); }), 0);
    CHECK(jumping > still);
    // An open string between fretted notes doesn't move the hand
    const float withOpen = partStars(guitarPart(120, 2, 64, [](int i){ return std::make_pair(i % 2 ? 1 : 2, i % 2 ? 0 : 5); }), 0);
    const float sameFret = partStars(guitarPart(120, 2, 64, [](int i){ return std::make_pair(i % 2 ? 1 : 2, 5); }), 0);
    CHECK(withOpen == doctest::Approx(sameFret).epsilon(0.02));

    // A part with no notes, or no such part
    Chart empty = guitarPart(120, 1, 0, [](int){ return std::make_pair(0, 0); });
    CHECK(partStars(empty, 0) == 0.0f);
    CHECK(partStars(empty, 3) == 0.0f);
}

TEST_CASE("stars: a keys part, after the fretted ones"){
    Chart chart = guitarPart(100, 1, 8, [](int){ return std::make_pair(0, 0); });
    KeysTrack keys;
    for (int i = 0; i < 64; i++) keys.notes.push_back({ i * 240, 60 + (i % 2) * 12, 0 }); // eighths, octave jumps
    chart.keysTracks = { keys };
    CHECK(partStars(chart, 1) > partStars(chart, 0));
}

TEST_CASE("pp: accuracy above all, then misses; only on the instrument"){
    const float perfect = runPerformance(4.0f, 100.0f, 0, 500);
    CHECK(perfect > 100.0f);
    CHECK(runPerformance(4.0f, 95.0f, 0, 500) < perfect);
    CHECK(runPerformance(4.0f, 95.0f, 5, 500) < runPerformance(4.0f, 95.0f, 0, 500));
    CHECK(runPerformance(5.0f, 95.0f, 0, 500) > runPerformance(4.0f, 95.0f, 0, 500)); // harder parts are worth more
    CHECK(runPerformance(4.0f, 100.0f, 0, 2000) > perfect);                           // and longer ones a little
    CHECK(runPerformance(4.0f, 100.0f, 0, 500, false) == 0.0f);                      // the computer keyboard isn't ranked
    CHECK(runPerformance(0.0f, 100.0f, 0, 500) == 0.0f);
}

TEST_CASE("total pp: the best first, each worth 95% of the one before"){
    CHECK(totalPerformance({}) == 0.0f);
    CHECK(totalPerformance({ 100.0f }) == doctest::Approx(100.0f));
    CHECK(totalPerformance({ 50.0f, 100.0f }) == doctest::Approx(100.0f + 50.0f * 0.95f)); // sorted first
    CHECK(performanceWeight(0) == doctest::Approx(1.0f));
    CHECK(performanceWeight(2) == doctest::Approx(0.9025f));
}
