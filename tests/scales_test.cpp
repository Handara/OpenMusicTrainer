#include "doctest/doctest.h"

#include "core/scales.h"

#include <string>

static const std::vector<int> STANDARD = { 40, 45, 50, 55, 59, 64 };

// Frets per string, low to high, the way guitar books draw a box
static std::vector<std::vector<int>> byString(const std::vector<FretPosition>& positions){
    std::vector<std::vector<int>> strings(6);
    for (const FretPosition& p : positions) strings[p.stringIndex].push_back(p.fret);
    return strings;
}

TEST_CASE("note names"){
    int pc;
    CHECK((parsePitchClass("C", pc) && pc == 0));
    CHECK((parsePitchClass("F#", pc) && pc == 6));
    CHECK((parsePitchClass("Bb", pc) && pc == 10));
    CHECK((parsePitchClass("g", pc) && pc == 7));
    CHECK((parsePitchClass("Cb", pc) && pc == 11));
    CHECK_FALSE(parsePitchClass("H", pc));
    CHECK_FALSE(parsePitchClass("C##", pc));
}

TEST_CASE("scale pitches"){
    const ScaleInfo* major = findScale("major");
    REQUIRE(major);
    CHECK(scalePitches(0, *major, 60, 72) == std::vector<int>{60, 62, 64, 65, 67, 69, 71, 72}); // C major
    CHECK(scalePitches(9, *findScale("minor_pentatonic"), 57, 69) == std::vector<int>{57, 60, 62, 64, 67, 69}); // A minor pentatonic
    CHECK(findScale("kazoo") == nullptr);
    CHECK(allScales().size() >= 13);
}

TEST_CASE("G major, 2nd position: the standard two-octave box"){
    std::vector<int> pitches = scalePitches(7, *findScale("major"), 43, 67); // G2 to G4
    std::vector<FretPosition> positions;
    std::string error;
    REQUIRE_MESSAGE(fingerPitches(pitches, STANDARD, Fingering::Position, 2, positions, error), error);
    std::vector<std::vector<int>> expected = {{3, 5}, {2, 3, 5}, {2, 4, 5}, {2, 4, 5}, {3, 5}, {2, 3}};
    CHECK(byString(positions) == expected);
}

TEST_CASE("A minor pentatonic, 5th position: the classic box"){
    std::vector<int> pitches = scalePitches(9, *findScale("minor_pentatonic"), 45, 69); // A2 to A4
    std::vector<FretPosition> positions;
    std::string error;
    REQUIRE_MESSAGE(fingerPitches(pitches, STANDARD, Fingering::Position, 5, positions, error), error);
    std::vector<std::vector<int>> expected = {{5, 8}, {5, 7}, {5, 7}, {5, 7}, {5, 8}, {5}};
    CHECK(byString(positions) == expected);
}

TEST_CASE("E major in open position uses open strings and frets up to 4"){
    std::vector<int> pitches = scalePitches(4, *findScale("major"), 40, 52); // E2 to E3
    std::vector<FretPosition> positions;
    std::string error;
    REQUIRE_MESSAGE(fingerPitches(pitches, STANDARD, Fingering::Position, 0, positions, error), error);
    std::vector<std::vector<int>> expected = {{0, 2, 4}, {0, 2, 4}, {1, 2}, {}, {}, {}};
    CHECK(byString(positions) == expected);
}

TEST_CASE("three notes per string"){
    std::vector<int> pitches = scalePitches(0, *findScale("major"), 48, 64); // C3 to E4: 10 notes
    std::vector<FretPosition> positions;
    std::string error;
    REQUIRE_MESSAGE(fingerPitches(pitches, STANDARD, Fingering::ThreeNotesPerString, 0, positions, error), error);
    std::vector<std::vector<int>> expected = {{8, 10, 12}, {8, 10, 12}, {9, 10, 12}, {9}, {}, {}};
    CHECK(byString(positions) == expected);
}

TEST_CASE("notes that can't be placed are reported"){
    std::vector<FretPosition> positions;
    std::string error;
    CHECK_FALSE(fingerPitches({90}, STANDARD, Fingering::Position, 2, positions, error)); // far above the neck
    CHECK(error.find("doesn't fit around fret 2") != std::string::npos);
    CHECK_FALSE(fingerPitches({35}, STANDARD, Fingering::Position, 0, positions, error)); // below the low E
}

TEST_CASE("the key signature a scale is written in"){
    struct Case { const char* root; const char* scale; int fifths; bool minor; };
    const Case cases[] = {
        {"G", "major", 1, false},  {"Bb", "major", -2, false}, {"F#", "major", 6, false},
        {"E", "minor", 1, true},   {"A", "minor_pentatonic", 0, true}, {"D", "dorian", 0, true},
        {"A", "blues", 0, true},   {"C", "chromatic", 0, false}, {"E", "harmonic_minor", 1, true},
    };
    for (const Case& c : cases){
        CAPTURE(c.root);
        CAPTURE(c.scale);
        int root;
        REQUIRE(parsePitchClass(c.root, root));
        KeySignature key = scaleKeySignature(root, *findScale(c.scale));
        CHECK(key.fifths == c.fifths);
        CHECK(key.minor == c.minor);
    }
}
