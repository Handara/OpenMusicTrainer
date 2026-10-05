#include "doctest/doctest.h"

#include "core/necktrainer.h"

#include <algorithm>
#include <vector>

static const std::vector<int> GUITAR = { 40, 45, 50, 55, 59, 64 };

static std::vector<int> pitchesOf(const std::vector<NeckStep>& steps){
    std::vector<int> pitches;
    for (const NeckStep& step : steps) pitches.push_back(step.pitch);
    return pitches;
}

TEST_CASE("the neck trainer: a scale up and down through a position"){
    NeckRoutine routine;
    routine.rootPitchClass = 7; // G major, index finger on fret 2: the open-ish G major box
    routine.position = 2;
    std::vector<NeckStep> steps;
    std::string error;
    REQUIRE_MESSAGE(neckSteps(routine, GUITAR, 22, steps, error), error);
    // Up then down, the top note once: a palindrome
    std::vector<int> pitches = pitchesOf(steps);
    CHECK(std::equal(pitches.begin(), pitches.end(), pitches.rbegin()));
    const size_t half = pitches.size() / 2 + 1;
    CHECK(std::is_sorted(pitches.begin(), pitches.begin() + (long)half));
    for (const NeckStep& step : steps){
        CHECK(step.fret >= 2);
        CHECK(step.fret <= 5);                        // within the hand's reach
        CHECK(GUITAR[step.string] + step.fret == step.pitch);
        const int degree = (step.pitch - 7 + 120) % 12;
        CHECK((degree == 0 || degree == 2 || degree == 4 || degree == 5 || degree == 7 || degree == 9 || degree == 11));
    }
    CHECK(steps.front().pitch == 42); // F#2, the low string's first note in reach
}

TEST_CASE("the neck trainer: thirds and triads go up in steps of the scale"){
    NeckRoutine routine;
    routine.rootPitchClass = 0; // C major, position 7
    routine.position = 7;
    std::vector<NeckStep> straight, thirds, triads;
    std::string error;
    REQUIRE(neckSteps(routine, GUITAR, 22, straight, error));
    routine.pattern = NeckPattern::Thirds;
    REQUIRE(neckSteps(routine, GUITAR, 22, thirds, error));
    routine.pattern = NeckPattern::Triads;
    REQUIRE(neckSteps(routine, GUITAR, 22, triads, error));
    const int n = (int)(straight.size() + 1) / 2; // the notes going up
    CHECK(thirds.size() == (size_t)(2 * (n - 2) * 2));
    CHECK(triads.size() == (size_t)(3 * (n - 4) * 2));
    // 1-3, 2-4: each pair skips one of the scale's notes
    CHECK(thirds[0].pitch == straight[0].pitch);
    CHECK(thirds[1].pitch == straight[2].pitch);
    CHECK(thirds[2].pitch == straight[1].pitch);
    CHECK(triads[2].pitch == straight[4].pitch);
}

TEST_CASE("the neck trainer: one note on every string, up and back"){
    NeckRoutine routine;
    routine.pattern = NeckPattern::EveryString;
    routine.notePitchClass = 0; // C, from the nut
    std::vector<NeckStep> steps;
    std::string error;
    REQUIRE(neckSteps(routine, GUITAR, 22, steps, error));
    REQUIRE(steps.size() == 11);
    const int frets[] = { 8, 3, 10, 5, 1, 8 }; // low E to high E
    for (int string = 0; string < 6; string++){
        CHECK(steps[string].string == string);
        CHECK(steps[string].fret == frets[string]);
    }
    CHECK(steps[6].string == 4); // and back down
}

TEST_CASE("the neck trainer: a run counts the next note, mistakes and the time each took"){
    NeckRun run;
    startNeckRun(run, { { 60, 1, 3 }, { 62, 1, 5 }, { 64, 2, 2 } });
    CHECK_FALSE(playNeckNote(run, 61, 1.0)); // wrong
    CHECK(run.mistakes == 1);
    CHECK(playNeckNote(run, 60, 1.5));
    CHECK(playNeckNote(run, 62, 2.0));
    CHECK_FALSE(neckRunDone(run));
    CHECK(playNeckNote(run, 64, 3.0));
    CHECK(neckRunDone(run));
    CHECK(neckRunSeconds(run) == doctest::Approx(1.5));
    CHECK(run.stepSeconds == std::vector<float>{ 0.0f, 0.5f, 1.0f });
}
