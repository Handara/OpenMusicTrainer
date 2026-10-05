#include "doctest/doctest.h"

#include "core/neckwalk.h"

#include <cstdio>
#include <filesystem>
#include <vector>

static const std::vector<int> GUITAR = { 40, 45, 50, 55, 59, 64 };
static const double START = 10.0;
static const float TEMPO = 120.0f; // half a second a beat

// Every walk note of the round played right on its beat, then time to the next round
static NeckWalkEvents playRound(NeckWalkGame& game, bool right){
    for (int i = 0; i < (int)game.walk.size(); i++){
        const double at = neckWalkNoteTime(game, i);
        neckWalkPlayed(game, right ? game.walk[i].pitch : game.walk[i].pitch + 1, at + 0.02);
        neckWalkUpdate(game, at + 0.1);
    }
    NeckWalkEvents verdict = neckWalkUpdate(game, neckWalkVerdictTime(game));
    if (!game.over) neckWalkUpdate(game, neckWalkRoundStart(game, game.round + 1));
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

TEST_CASE("neck walk: the computer plays the walk, the player plays it back; all right is cheered"){
    NeckWalkGame game;
    startNeckWalk(game, 0, GUITAR, 7, START, TEMPO);
    REQUIRE(game.walk.size() == 4);
    const int root = game.root;
    // A bar to get ready; the computer's lick, a note every two beats; the player's, two bars later; the verdict half
    // a beat before the next round
    CHECK(neckWalkRoundStart(game, 0) == doctest::Approx(START + 2.0));
    CHECK(neckWalkShowTime(game, 0) == doctest::Approx(START + 2.0));
    CHECK(neckWalkShowTime(game, 3) == doctest::Approx(START + 5.0));
    CHECK(neckWalkNoteTime(game, 0) == doctest::Approx(START + 6.0));
    CHECK(neckWalkNoteTime(game, 3) == doctest::Approx(START + 9.0));
    CHECK(neckWalkVerdictTime(game) == doctest::Approx(START + 9.75));
    CHECK(neckWalkRoundStart(game, 1) == doctest::Approx(START + 10.0));
    // The next round's walk is known a round ahead, a fourth up
    CHECK(game.nextRoot == (root + 5) % 12);
    const std::vector<NeckStep> next = game.nextWalk;
    REQUIRE(next.size() == 4);
    // The computer's notes played back while it plays them count for nothing
    CHECK(neckWalkPlayed(game, game.walk[0].pitch, neckWalkShowTime(game, 0)).right < 0);
    NeckWalkEvents verdict = playRound(game, true);
    CHECK(verdict.cheer);
    CHECK_FALSE(verdict.aww);
    CHECK(game.round == 1);
    CHECK(game.root == (root + 5) % 12);
    CHECK(game.walk[0].pitch == next[0].pitch);
    CHECK(game.streak == 1);
    CHECK(game.cleared == 1);
    CHECK(game.lives == 5);
    CHECK(game.score == 4 * 100 + 5 * 100);
    // Its own octave isn't asked for: an octave up counts
    const double at = neckWalkNoteTime(game, 0);
    CHECK(neckWalkPlayed(game, game.walk[0].pitch + 12, at).right == 0);
}

TEST_CASE("neck walk: a slip put right in time counts; a wrong note or none costs the round"){
    NeckWalkGame game;
    startNeckWalk(game, 0, GUITAR, 3, START, TEMPO);
    double at = neckWalkNoteTime(game, 0);
    CHECK(neckWalkPlayed(game, game.walk[0].pitch + 1, at - 0.1).right < 0); // wrong, for now
    CHECK(neckWalkPlayed(game, game.walk[0].pitch, at + 0.05).right == 0);   // then right: right
    CHECK(neckWalkPlayed(game, game.walk[1].pitch, at + 0.5).right < 0);     // far from any beat: let go
    at = neckWalkNoteTime(game, 1);
    neckWalkPlayed(game, game.walk[1].pitch + 2, at);
    CHECK(neckWalkUpdate(game, at + 0.3).wrong == 1);
    CHECK(neckWalkUpdate(game, neckWalkNoteTime(game, 2) + 0.3).missed == 2);
    CHECK(game.notes[0] == WalkNote::Right);
    NeckWalkEvents verdict = neckWalkUpdate(game, neckWalkVerdictTime(game));
    CHECK(verdict.aww);
    CHECK(game.lives == 4);
    CHECK(game.streak == 0);
}

TEST_CASE("neck walk: the levels walk more strings, faster, and every walk fits its lick"){
    CHECK(neckWalkLevelIndex("hard") == 2);
    CHECK(neckWalkLevelIndex("insane") == -1);
    for (int i = 0; i < NECK_WALK_LEVELS; i++){
        const NeckWalkLevel& level = neckWalkLevel(i);
        CHECK(level.notes * level.beatsPerNote <= NECK_WALK_PART_BEATS);
        NeckWalkGame game;
        startNeckWalk(game, i, GUITAR, 21, START, TEMPO);
        REQUIRE(game.walk.size() == (size_t)level.notes);
        for (const NeckStep& step : game.walk) CHECK(step.fret <= level.maxFret);
        // The last note's window closes before the verdict
        CHECK(neckWalkNoteTime(game, level.notes - 1) + level.windowSeconds < neckWalkVerdictTime(game));
    }
    // A bass has four strings: hard walks all of them
    NeckWalkGame bass;
    startNeckWalk(bass, 2, { 28, 33, 38, 43 }, 4, START, TEMPO);
    CHECK(bass.walk.size() == 8);
}

TEST_CASE("neck walk: five rounds wrong and the game is over"){
    NeckWalkGame game;
    startNeckWalk(game, 0, GUITAR, 11, START, TEMPO);
    playRound(game, true);
    for (int i = 0; i < 4; i++){
        CHECK(playRound(game, false).aww);
        CHECK_FALSE(game.over);
    }
    NeckWalkEvents last = playRound(game, false);
    CHECK(last.over);
    CHECK(game.over);
    CHECK(game.cleared == 1);
    // Nothing more happens
    CHECK_FALSE(neckWalkUpdate(game, 1000.0).newRound);
}

TEST_CASE("neck walk: games are kept, with each note's walk notes right and wrong"){
    const std::string path = (std::filesystem::temp_directory_path() / "lahn-neckwalk-test.txt").string();
    std::remove(path.c_str());
    NeckWalkStats stats = loadNeckWalkStats(path);
    CHECK(stats.games.empty());
    CHECK(stats.lastLevel == -1);
    CHECK(neckWalkBest(stats, 1) == 0);
    NeckWalkGame game;
    startNeckWalk(game, 1, GUITAR, 5, START, TEMPO);
    const int root = game.root;
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
    CHECK(loaded.right[root] == 6);
    CHECK(neckWalkBest(loaded, 1) == game.score); // the best of its level
    CHECK(neckWalkBest(loaded, 0) == 0);
    CHECK(loaded.lastLevel == 1);                 // the choice, for next time
    CHECK(loaded.lastBpm == 120);
    std::remove(path.c_str());
}
