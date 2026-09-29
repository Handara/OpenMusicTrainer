#include "doctest/doctest.h"

#include "core/positions.h"

const std::vector<int> GUITAR = { 40, 45, 50, 55, 59, 64 };

TEST_CASE("every place a pitch lives on the neck"){
    std::vector<StringFret> e4 = positionsOf(64, GUITAR, 22);
    // The high E open, the B's 5th, the G's 9th, the D's 14th, the A's 19th
    REQUIRE(e4.size() == 5);
    CHECK(e4[0] == StringFret{1, 19});
    CHECK(e4[3] == StringFret{4, 5});
    CHECK(e4[4] == StringFret{5, 0});
    CHECK(positionsOf(40, GUITAR, 22).size() == 1);  // the low E: only one place
    CHECK(positionsOf(30, GUITAR, 22).empty());      // below the guitar
}

TEST_CASE("the likeliest place: near where the hand was"){
    std::vector<StringFret> e4 = positionsOf(64, GUITAR, 22);
    CHECK(likeliestPosition(e4, {-1, -1}) == StringFret{5, 0});  // nothing before: the lowest fret, the open string
    CHECK(likeliestPosition(e4, {3, 9}) == StringFret{3, 9});    // around the 9th fret: the G string's 9th
    CHECK(likeliestPosition(e4, {4, 6}) == StringFret{4, 5});    // around the 5th: the B's 5th
    CHECK(likeliestPosition(e4, {1, 18}) == StringFret{1, 19});  // way up the neck: the A's 19th
    CHECK(likeliestPosition({}, {2, 2}).string == -1);
}
