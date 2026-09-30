#include "doctest/doctest.h"

#include "core/tuningcheck.h"

const std::vector<int> BASS = { 28, 33, 38, 43 }; // E1 A1 D2 G2

TEST_CASE("an open string is known by its note, even heard an octave up"){
    CHECK(openStringHeard(28.0f, BASS) == 0);
    CHECK(openStringHeard(40.1f, BASS) == 0);  // E2: the low E's octave, not the D string two half steps under it
    CHECK(openStringHeard(33.4f, BASS) == 1);  // a sharp A
    CHECK(openStringHeard(42.7f, BASS) == 3);  // a flat G
    CHECK(openStringHeard(60.0f, BASS) == -1); // middle C is no open string of a bass
    CHECK(openStringHeard(38.0f, { 26, 33, 38, 43 }) == 2); // drop D: the D string, D1 is an octave under
    CHECK(centsOff(28.3f, 28) == doctest::Approx(30.0f));
    CHECK(centsOff(39.8f, 28) == doctest::Approx(-20.0f)); // the octave aside
}

TEST_CASE("the check before a song: every string held in tune for a moment"){
    TuningCheck check = startTuningCheck(BASS);
    CHECK_FALSE(allTuned(check));
    // A pluck passing through in tune isn't enough; settled in tune it is
    hearForTuning(check, 28.05f, 0.1f);
    CHECK(check.strings[0].heard);
    CHECK_FALSE(check.strings[0].tuned);
    hearForTuning(check, 28.3f, 0.1f); // off again: starts over
    CHECK(check.strings[0].cents == doctest::Approx(30.0f));
    for (int i = 0; i < 4; i++) hearForTuning(check, 28.05f, 0.1f);
    CHECK(check.strings[0].tuned);
    hearForTuning(check, 28.4f, 0.1f); // once tuned, it stays: the check is about getting there
    CHECK(check.strings[0].tuned);
    CHECK(check.lastString == 0);
    for (int string = 1; string < 4; string++){
        for (int i = 0; i < 4; i++) hearForTuning(check, (float)BASS[string] - 0.04f, 0.1f);
    }
    CHECK(allTuned(check));
    hearForTuning(check, 0.0f, 0.1f); // nothing heard changes nothing
    CHECK(check.lastString == 3);
}

TEST_CASE("while playing: an instrument out of tune, not a bend or a slip"){
    float cents = 0.0f;
    TuningWatch inTune;
    for (float offset : { 5.0f, -8.0f, 12.0f, 3.0f, 40.0f, -2.0f, 6.0f, 0.0f, 9.0f }) watchTuning(inTune, offset);
    CHECK_FALSE(looksOutOfTune(inTune, cents)); // one bend among them

    TuningWatch sharp;
    for (float offset : { 32.0f, 41.0f, 36.0f, 28.0f }) watchTuning(sharp, offset);
    CHECK_FALSE(looksOutOfTune(sharp, cents)); // too few notes to say yet
    for (float offset : { 38.0f, 45.0f, 33.0f, 39.0f }) watchTuning(sharp, offset);
    REQUIRE(looksOutOfTune(sharp, cents));
    CHECK(cents == doctest::Approx(37.0f).epsilon(0.05));

    TuningWatch flat;
    for (int i = 0; i < 8; i++) watchTuning(flat, -45.0f + (i % 3) * 5.0f);
    REQUIRE(looksOutOfTune(flat, cents));
    CHECK(cents < -30.0f);

    TuningWatch slips; // wrong notes a half step off aren't the instrument
    for (int i = 0; i < 8; i++) watchTuning(slips, i % 2 ? 100.0f : 4.0f);
    CHECK_FALSE(looksOutOfTune(slips, cents));

    TuningWatch mixed; // half of them sour, half fine: the player, sliding into notes
    for (int i = 0; i < 8; i++) watchTuning(mixed, i % 2 ? 45.0f : 0.0f);
    CHECK_FALSE(looksOutOfTune(mixed, cents));
}
