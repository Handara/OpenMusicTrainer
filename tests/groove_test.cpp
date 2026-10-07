#include "doctest/doctest.h"

#include "core/groove.h"

#include <string>

TEST_CASE("groove: bars of guitar, bass and drums, in a phrase, played in any key"){
    Groove groove;
    std::string error;
    REQUIRE_MESSAGE(parseGroove("# a comment\n"
                                "tempo 138\n"
                                "key F#   # written in F#\n"
                                "phrase call riff end\n"
                                "call,riff guitar 2.5 0.5 C#3\n"
                                "riff bass 0 1 F#1\n"
                                "call drums 0 crash\n"
                                "end guitar 2 2 F#3\n",
                                groove, error), error);
    CHECK(groove.tempo == doctest::Approx(138.0f));
    CHECK(groove.key == 6);
    REQUIRE(groove.phrase.size() == 3);
    REQUIRE(groove.bars["call"].size() == 2); // the guitar line's both bars, and the crash
    CHECK(groove.bars["riff"].size() == 2);
    const GrooveHit& note = groove.bars["call"][0];
    CHECK(note.part == GroovePart::Guitar);
    CHECK(note.beat == doctest::Approx(2.5));
    CHECK(note.length == doctest::Approx(0.5));
    CHECK(note.pitch == 49); // C#3
    CHECK(groove.bars["call"][1].drum == KitDrum::Crash);
    // In A: three up; in E: two down (not ten up); the drums don't move
    CHECK(grooveShift(groove, 9) == 3);
    CHECK(grooveShift(groove, 4) == -2);
    CHECK(grooveShift(groove, 3) == 9);
    std::vector<GrooveHit> bar = grooveBar(groove, 0, 9);
    CHECK(bar[0].pitch == 52);
    CHECK(bar[1].part == GroovePart::Drums);
    // The phrase goes round
    CHECK(grooveBar(groove, 4, 6)[1].part == GroovePart::Bass); // bar 4 of 3: the riff, its guitar then its bass
}

TEST_CASE("groove: mistakes are told by their line"){
    Groove groove;
    std::string error;
    CHECK_FALSE(parseGroove("phrase a\na guitar 0 1 H3\n", groove, error));
    CHECK(error.find("line 2") != std::string::npos);
    CHECK_FALSE(parseGroove("phrase a\na drums 0 cowbell\n", groove, error));
    CHECK_FALSE(parseGroove("phrase a b\na drums 0 kick\n", groove, error)); // b has nothing in it
    CHECK(error.find("'b'") != std::string::npos);
    CHECK_FALSE(parseGroove("a drums 0 kick\n", groove, error)); // no phrase
}

TEST_CASE("groove: neck walk's own tune loads"){
    Groove groove;
    std::string error;
    REQUIRE_MESSAGE(loadGroove(std::string(HARDTHZ_RESOURCES_DIR) + "games/neck-walk.groove", groove, error), error);
    CHECK(groove.phrase.size() == 4);
    CHECK(groove.key == 6);
    CHECK(groove.bars["end"].size() > 5);
}
