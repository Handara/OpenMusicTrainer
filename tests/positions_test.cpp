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

TEST_CASE("the part of the neck a song needs"){
    // Low notes: from the nut, at least 7 frets
    CHECK(fretSpanFor({0, 2, 3}, 7, 22).first == 0);
    CHECK(fretSpanFor({0, 2, 3}, 7, 22).last == 7);
    // A solo at the 12th fret: from the 11th, wide enough to read
    FretSpan solo = fretSpanFor({12, 14, 15, 17}, 7, 22);
    CHECK(solo.first == 11);
    CHECK(solo.last == 18);
    // Up at the top of the neck: widened down instead of past the last fret
    FretSpan top = fretSpanFor({20, 22}, 7, 22);
    CHECK(top.last == 22);
    CHECK(top.first == 15);
    // A song all over the neck shows all of it
    FretSpan all = fretSpanFor({0, 21}, 7, 22);
    CHECK(all.first == 0);
    CHECK(all.last == 22);
    CHECK(fretSpanFor({}, 7, 22).last == 7);
}
