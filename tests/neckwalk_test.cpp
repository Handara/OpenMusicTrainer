#include "doctest/doctest.h"

#include "core/neckwalk.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <vector>

static const std::vector<int> GUITAR = { 40, 45, 50, 55, 59, 64 };
static const std::vector<int> BASS = { 28, 33, 38, 43 };
static const double START = 10.0;
static const float TEMPO = 120.0f; // half a second a beat

// Every note of the player's played (right, or a semitone off) on its beat, then time to the next round
static NeckWalkEvents playRound(NeckWalkGame& game, bool right){
    for (int i = 0; i < (int)game.now.walk.size(); i++){
        const double at = neckWalkNoteTime(game, i);
        neckWalkPlayed(game, right ? game.now.walk[i].pitch : game.now.walk[i].pitch + 1, at + 0.02);
        neckWalkUpdate(game, at + 0.05);
    }
    NeckWalkEvents verdict = neckWalkUpdate(game, neckWalkVerdictTime(game));
    if (!game.over) neckWalkUpdate(game, neckWalkRoundStart(game, game.next));
    return verdict;
}

TEST_CASE("neck walk: the note on strings side by side, from the highest down and back up"){
    // A on D, G and B, starting near fret 7: D7, then G2 (the A nearest), then B10
    std::vector<NeckStep> walk = neckWalkSteps(9, GUITAR, 2, 3, 7, 12, 5);
    REQUIRE(walk.size() == 5);
    CHECK(walk[0].string == 4);
    CHECK(walk[0].fret == 10);
    CHECK(walk[1].string == 3);
    CHECK(walk[1].fret == 2);
    CHECK(walk[2].string == 2);
    CHECK(walk[2].fret == 7);
    CHECK(walk[3].string == 3);
    CHECK(walk[4].string == 4);
    for (const NeckStep& step : walk) CHECK(step.pitch % 12 == 9);
    // Two strings bounce: high, low, high, low, high
    walk = neckWalkSteps(2, GUITAR, 0, 2, 5, 12, 5); // D on the low E and the A string
    REQUIRE(walk.size() == 5);
    CHECK(walk[0].string == 1);
    CHECK(walk[1].string == 0);
    CHECK(walk[2].string == 1);
    CHECK(walk[0].fret == 5);  // D3 on the A string
    CHECK(walk[1].fret == 10); // D on the low E nearest fret 5
    // Not above the highest fret allowed
    CHECK(neckWalkSteps(9, GUITAR, 2, 3, 7, 1, 5).empty());
}

TEST_CASE("neck walk: a triad in one place on the neck, lowest first"){
    // C major, frets 3 to 6 on the low four strings: G2 (E3), C3 (A3), G3 (D5), C4 (G5); no E in that box
    std::vector<NeckStep> triad = neckWalkTriad(0, GUITAR, 0, 4, 3);
    REQUIRE(triad.size() == 4);
    CHECK(triad[0].pitch == 43);
    CHECK(triad[3].pitch == 60);
    CHECK(std::is_sorted(triad.begin(), triad.end(), [](const NeckStep& a, const NeckStep& b){ return a.pitch < b.pitch; }));
    for (const NeckStep& step : triad){
        const int degree = step.pitch % 12;
        CHECK((degree == 0 || degree == 4 || degree == 7));
        CHECK(step.fret >= 3);
        CHECK(step.fret <= 6);
        CHECK(step.string < 4);
    }
}

TEST_CASE("neck walk: rhythms fit two bars, or four when long, a beat to spare"){
    std::mt19937 random(5);
    int bars = 0;
    std::vector<double> onsets = neckWalkRhythm(NeckWalkRhythm::Even, 5, random, bars);
    CHECK(bars == 2);
    CHECK(onsets == std::vector<double>{ 0, 1, 2, 3, 4 });
    onsets = neckWalkRhythm(NeckWalkRhythm::Even, 11, random, bars);
    CHECK(bars == 4);
    CHECK(onsets.back() == doctest::Approx(10.0));
    for (NeckWalkRhythm rhythm : { NeckWalkRhythm::Mixed, NeckWalkRhythm::Syncopated }){
        for (int notes = 3; notes <= NECK_WALK_MAX_NOTES; notes++){
            onsets = neckWalkRhythm(rhythm, notes, random, bars);
            REQUIRE(onsets.size() == (size_t)notes);
            CHECK(std::is_sorted(onsets.begin(), onsets.end()));
            CHECK((bars == 2 || bars == 4));
            CHECK(onsets.back() <= bars * NECK_WALK_BAR_BEATS - 1);
            for (double onset : onsets) CHECK(std::fmod(onset * 2.0, 1.0) == doctest::Approx(0.0)); // on the eighths
        }
    }
}

TEST_CASE("neck walk: the computer plays the sequence, the player plays it back; all right is cheered"){
    NeckWalkGame game;
    startNeckWalk(game, 0, GUITAR, 7, START, TEMPO);
    const NeckWalkRound first = game.now;
    REQUIRE(first.walk.size() >= 5);           // easy: three strings to six, down and back up
    CHECK(first.walk.size() % 2 == 1);
    // A bar to get ready; the computer's part on the beats; the player's, a part later; the verdict half a beat before
    // the next round
    const double part = first.partBars * 4 * 0.5;
    CHECK(neckWalkRoundStart(game, game.now) == doctest::Approx(START + 2.0));
    CHECK(neckWalkShowTime(game, game.now, 1) == doctest::Approx(START + 2.5));
    CHECK(neckWalkNoteTime(game, 0) == doctest::Approx(START + 2.0 + part));
    CHECK(neckWalkVerdictTime(game) == doctest::Approx(START + 2.0 + 2 * part - 0.25));
    CHECK(neckWalkRoundStart(game, game.next) == doctest::Approx(START + 2.0 + 2 * part));
    // The next round is known a round ahead, a fourth up
    CHECK(game.next.root == (first.root + 5) % 12);
    const NeckWalkRound next = game.next;
    // Notes played back while the computer plays count for nothing
    CHECK(neckWalkPlayed(game, first.walk[0].pitch, neckWalkShowTime(game, game.now, 0)).right < 0);
    NeckWalkEvents verdict = playRound(game, true);
    CHECK(verdict.verdict);
    CHECK(verdict.how == NeckWalkVerdict::Cheer);
    CHECK(game.round == 1);
    CHECK(game.now.root == next.root);
    CHECK(game.now.walk[0].pitch == next.walk[0].pitch);
    CHECK(game.streak == 1);
    CHECK(game.cleared == 1);
    CHECK(game.lives == 5);
    CHECK(game.score == (long long)first.walk.size() * 100 + 5 * 100);
    // Its own octave isn't asked for: an octave up counts
    CHECK(neckWalkPlayed(game, game.now.walk[0].pitch + 12, neckWalkNoteTime(game, 0)).right == 0);
}

TEST_CASE("neck walk: a slip put right in time counts; a wrong note or none costs the round"){
    NeckWalkGame game;
    startNeckWalk(game, 0, GUITAR, 3, START, TEMPO);
    const std::vector<NeckStep>& walk = game.now.walk;
    double at = neckWalkNoteTime(game, 0);
    CHECK(neckWalkPlayed(game, walk[0].pitch + 1, at - 0.1).right < 0); // wrong, for now
    CHECK(neckWalkPlayed(game, walk[0].pitch, at + 0.05).right == 0);   // then right: right
    CHECK(neckWalkPlayed(game, walk[1].pitch, at + 0.25).right < 0);    // far from any beat: let go
    at = neckWalkNoteTime(game, 1);
    neckWalkPlayed(game, walk[1].pitch + 2, at);
    CHECK(neckWalkUpdate(game, at + 0.24).wrong == 1);
    CHECK(neckWalkUpdate(game, neckWalkNoteTime(game, 2) + 0.24).missed == 2);
    CHECK(game.notes[0] == WalkNote::Right);
    NeckWalkEvents verdict = neckWalkUpdate(game, neckWalkVerdictTime(game));
    CHECK(verdict.how == NeckWalkVerdict::Aww); // one right of five or more
    CHECK(game.lives == 4);
    CHECK(game.streak == 0);
}

TEST_CASE("neck walk: the levels go anywhere on the neck and differ in what's played"){
    CHECK(neckWalkLevelIndex("hard") == 2);
    CHECK(neckWalkLevelIndex("insane") == -1);
    for (int level = 0; level < NECK_WALK_LEVELS; level++){
        bool allStrings = false;
        for (unsigned seed = 1; seed <= 40; seed++){
            NeckWalkGame game;
            startNeckWalk(game, level, GUITAR, seed, START, TEMPO);
            const NeckWalkRound& round = game.now;
            REQUIRE(round.walk.size() >= 3);
            CHECK(round.walk.size() <= (size_t)NECK_WALK_MAX_NOTES);
            REQUIRE(round.onsets.size() == round.walk.size());
            CHECK(round.onsets.back() <= round.partBars * NECK_WALK_BAR_BEATS - 1);
            std::vector<int> strings;
            for (const NeckStep& step : round.walk){
                CHECK(step.fret <= neckWalkLevel(level).maxFret);
                CHECK(GUITAR[step.string] + step.fret == step.pitch);
                const int degree = ((step.pitch - round.root) % 12 + 12) % 12;
                if (level < 2) CHECK(degree == 0);                       // the key's note
                else CHECK((degree == 0 || degree == 4 || degree == 7)); // its triad, the third in it
                if (std::find(strings.begin(), strings.end(), step.string) == strings.end()) strings.push_back(step.string);
            }
            if (level == 2) CHECK(std::any_of(round.walk.begin(), round.walk.end(), [&](const NeckStep& step){ return (step.pitch - round.root + 120) % 12 == 4; }));
            allStrings = allStrings || strings.size() == GUITAR.size();
            // The last note's window closes before the verdict
            CHECK(neckWalkNoteTime(game, (int)round.walk.size() - 1) + neckWalkLevel(level).windowSeconds < neckWalkVerdictTime(game));
        }
        CHECK(allStrings); // every level, all six strings now and then
    }
    // A bass: its four strings
    NeckWalkGame bass;
    startNeckWalk(bass, 1, BASS, 4, START, TEMPO);
    REQUIRE_FALSE(bass.now.walk.empty());
    for (const NeckStep& step : bass.now.walk) CHECK(step.string < 4);
}

TEST_CASE("neck walk: half the sequence right isn't a fail: claps, no life lost, but the streak starts again"){
    NeckWalkGame game;
    startNeckWalk(game, 0, GUITAR, 9, START, TEMPO);
    playRound(game, true);
    REQUIRE(game.streak == 1);
    for (int i = 0; i < (int)game.now.walk.size(); i++){
        const double at = neckWalkNoteTime(game, i);
        if (i % 2 == 0) neckWalkPlayed(game, game.now.walk[i].pitch, at); // more than half: easy's are odd in number
        neckWalkUpdate(game, at + 0.3);
    }
    CHECK(neckWalkVerdictOf(game) == NeckWalkVerdict::Claps);
    NeckWalkEvents verdict = neckWalkUpdate(game, neckWalkVerdictTime(game));
    CHECK(verdict.how == NeckWalkVerdict::Claps);
    CHECK(game.lives == NECK_WALK_LIVES);
    CHECK(game.streak == 0);
    CHECK(game.cleared == 1);
}

TEST_CASE("neck walk: five rounds wrong and the game is over"){
    NeckWalkGame game;
    startNeckWalk(game, 2, GUITAR, 11, START, TEMPO);
    playRound(game, true);
    for (int i = 0; i < 4; i++){
        CHECK(playRound(game, false).how == NeckWalkVerdict::Aww);
        CHECK_FALSE(game.over);
    }
    NeckWalkEvents last = playRound(game, false);
    CHECK(last.over);
    CHECK(game.over);
    CHECK(game.cleared == 1);
    // Nothing more happens
    CHECK_FALSE(neckWalkUpdate(game, 1000.0).newRound);
}

TEST_CASE("neck walk: games are kept, with each key's notes right and wrong, and the last level and tempo"){
    const std::string path = (std::filesystem::temp_directory_path() / "lahn-neckwalk-test.txt").string();
    std::remove(path.c_str());
    NeckWalkStats stats = loadNeckWalkStats(path);
    CHECK(stats.games.empty());
    CHECK(stats.lastLevel == -1);
    CHECK(neckWalkBest(stats, 1) == 0);
    NeckWalkGame game;
    startNeckWalk(game, 1, GUITAR, 5, START, TEMPO);
    const int root = game.now.root;
    const int notes = (int)game.now.walk.size();
    playRound(game, true);
    addNeckWalkGame(stats, game, "2026-10-05");
    std::string error;
    REQUIRE_MESSAGE(saveNeckWalkStats(path, stats, error), error);
    NeckWalkStats loaded = loadNeckWalkStats(path);
    REQUIRE(loaded.games.size() == 1);
    CHECK(loaded.games[0].score == game.score);
    CHECK(loaded.games[0].cleared == 1);
    CHECK(loaded.games[0].level == "normal");
    CHECK(loaded.games[0].bpm == 120);
    CHECK(loaded.right[root] == notes);
    CHECK(neckWalkBest(loaded, 1) == game.score); // the best of its level
    CHECK(neckWalkBest(loaded, 0) == 0);
    CHECK(loaded.lastLevel == 1);                 // the choice, for next time
    CHECK(loaded.lastBpm == 120);
    std::remove(path.c_str());
}
