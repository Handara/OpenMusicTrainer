#include "doctest/doctest.h"

#include "core/ranking.h"

#include <filesystem>

TEST_CASE("accuracy and grades, counted the osu! way"){
    CHECK(runAccuracy(10, 0, 0) == doctest::Approx(100.0f));
    CHECK(runAccuracy(0, 10, 0) == doctest::Approx(100.0f / 3));   // all goods: a third
    CHECK(runAccuracy(9, 0, 1) == doctest::Approx(90.0f));
    CHECK(runAccuracy(0, 0, 0) == 0.0f);

    CHECK(gradeFor(100.0f, 0) == Grade::SS);
    CHECK(gradeFor(96.0f, 0) == Grade::S);
    CHECK(gradeFor(96.0f, 1) == Grade::A);   // a miss keeps an S away, however accurate the rest
    CHECK(gradeFor(85.0f, 2) == Grade::B);
    CHECK(gradeFor(71.0f, 5) == Grade::C);
    CHECK(gradeFor(40.0f, 9) == Grade::D);
    CHECK(std::string(gradeName(Grade::SS)) == "SS");
}

TEST_CASE("timing stats: the average and the unstable rate"){
    TimingStats steady = timingStats({ 10.0f, 10.0f, 10.0f });
    CHECK(steady.meanMs == doctest::Approx(10.0f));  // always 10 ms early...
    CHECK(steady.unstableRate == doctest::Approx(0.0f)); // ...but perfectly steady
    TimingStats spread = timingStats({ -20.0f, 20.0f });
    CHECK(spread.meanMs == doctest::Approx(0.0f));
    CHECK(spread.unstableRate == doctest::Approx(200.0f)); // standard deviation 20 ms, times ten
    CHECK(timingStats({}).unstableRate == 0.0f);
}

static Chart smallChart(){
    Chart chart{};
    chart.resolution = 480;
    chart.tempoMap = {{0, 120.0}};
    FrettedTrack track;
    track.tuning = { 40, 45, 50, 55, 59, 64 };
    track.notes = {{0, 0, 3, 0}, {480, 1, 2, 0}};
    chart.frettedTracks = {track, track};
    return chart;
}

TEST_CASE("a part's fingerprint changes with what's played, and only then"){
    Chart chart = smallChart();
    std::string original = partFingerprint(chart, 0);
    CHECK(original.size() == 16);
    CHECK(partFingerprint(chart, 0) == original);
    chart.title = "Renamed";                      // not what's played: same fingerprint
    CHECK(partFingerprint(chart, 0) == original);
    chart.frettedTracks[0].notes[1].fret = 3;     // a note moved
    CHECK(partFingerprint(chart, 0) != original);
    Chart faster = smallChart();
    faster.tempoMap[0].bpm = 140.0;               // the same notes, faster
    CHECK(partFingerprint(faster, 0) != original);
    Chart trimmed = smallChart();
    trimmed.trimEnd = 1.5;                        // the same notes, but not all of them played
    CHECK(partFingerprint(trimmed, 0) != original);
}

TEST_CASE("records: the best first, a limited list, kept on disk"){
    std::vector<RunRecord> records;
    RunRecord run;
    run.score = 500;
    CHECK(addRun(records, run) == 0);
    run.score = 900;
    CHECK(addRun(records, run) == 0);  // a new best
    run.score = 700;
    CHECK(addRun(records, run) == 1);
    run.score = 700;
    CHECK(addRun(records, run) == 2);  // a tie goes after the one already there
    for (int i = 0; i < KEPT_RUNS; i++){ run.score = 1000 + i; addRun(records, run); }
    CHECK((int)records.size() == KEPT_RUNS);
    run.score = 1;
    CHECK(addRun(records, run) == -1); // not good enough to be kept
    CHECK(records.front().score == 1000 + KEPT_RUNS - 1);

    RunRecord detailed;
    detailed.score = 12345;
    detailed.accuracy = 97.5f;
    detailed.maxCombo = 88;
    detailed.perfect = 80; detailed.good = 8; detailed.miss = 0;
    detailed.unstableRate = 142.5f;
    detailed.withInstrument = true;
    detailed.date = "2026-09-29";
    std::filesystem::path path = std::filesystem::temp_directory_path() / "lahn_tests" / "records.txt";
    std::filesystem::create_directories(path.parent_path());
    std::string error;
    REQUIRE_MESSAGE(saveRuns(path.string(), {detailed}, error), error);
    std::vector<RunRecord> loaded = loadRuns(path.string());
    REQUIRE(loaded.size() == 1);
    CHECK(loaded[0].score == 12345);
    CHECK(loaded[0].accuracy == doctest::Approx(97.5f));
    CHECK(loaded[0].maxCombo == 88);
    CHECK(loaded[0].good == 8);
    CHECK(loaded[0].unstableRate == doctest::Approx(142.5f));
    CHECK(loaded[0].withInstrument);
    CHECK(loaded[0].date == "2026-09-29");
    CHECK(loaded[0].fullCombo());
    CHECK(loaded[0].grade() == Grade::S);
}

TEST_CASE("history: every run in the order played, started from the records kept before it"){
    std::filesystem::path dir = std::filesystem::temp_directory_path() / "lahn_tests" / "history";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    std::string records = (dir / "song-part2-abc.txt").string();
    std::string path = historyPath(records);
    CHECK(std::filesystem::path(path).filename() == "song-part2-abc-history.txt");

    // No history yet: the records seed it, oldest date first, whatever their scores
    auto runOn = [](int score, const char* date){ RunRecord run; run.score = score; run.date = date; return run; };
    std::vector<RunRecord> best = { runOn(900, "2026-09-30"), runOn(800, "2026-09-28"), runOn(700, "2026-09-30") };
    std::vector<RunRecord> history = loadHistory(path, best);
    REQUIRE(history.size() == 3);
    CHECK(history[0].score == 800);
    CHECK(history[1].score == 900); // the same day: kept in the records' order
    CHECK(history[2].score == 700);

    // A worse run still goes on the end, and it's all there next time
    std::string error;
    REQUIRE_MESSAGE(addToHistory(path, history, runOn(100, "2026-10-01"), error), error);
    std::vector<RunRecord> loaded = loadHistory(path, best);
    REQUIRE(loaded.size() == 4);
    CHECK(loaded.back().score == 100);
    CHECK(loaded.back().date == "2026-10-01");

    // Only the latest are kept
    for (int i = 0; i < KEPT_HISTORY; i++) REQUIRE(addToHistory(path, loaded, runOn(i, "2026-10-02"), error));
    CHECK((int)loaded.size() == KEPT_HISTORY);
    CHECK(loaded.front().score == 0);
    CHECK(loadHistory(path, best).back().score == KEPT_HISTORY - 1);
}
