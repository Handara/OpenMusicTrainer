#include "doctest/doctest.h"

#include "core/reading.h"

#include <algorithm>
#include <cstdlib>

TEST_CASE("the notes of C major in open position"){
    ReadingConfig config; // C major, frets 0 to 3, every string
    std::vector<DrillNote> notes = readingPositionNotes(config);
    // E2 F2 G2 | A2 B2 C3 | D3 E3 F3 | G3 A3 | B3 C4 D4 | E4 F4 G4
    std::vector<int> expected = { 40, 41, 43, 45, 47, 48, 50, 52, 53, 55, 57, 59, 60, 62, 64, 65, 67 };
    REQUIRE(notes.size() == expected.size());
    for (size_t i = 0; i < notes.size(); i++) CHECK(notes[i].pitch == expected[i]);
    // B3 is on the G string's 4th fret too, but that's out of the position: the open B string it is
    CHECK(notes[11].stringIndex == 4);
    CHECK(notes[11].fret == 0);
}

TEST_CASE("a reading melody stays in the position and moves like a melody"){
    ReadingConfig config;
    config.strings = { 1, 2 };       // the A and D strings
    config.maxLeap = 2;
    std::vector<DrillNote> position = readingPositionNotes(config);
    std::mt19937 rng(5);
    for (int pass = 0; pass < 30; pass++){
        std::vector<DrillNote> melody;
        std::string error;
        REQUIRE_MESSAGE(buildReading(config, rng, melody, error), error);
        REQUIRE_FALSE(melody.empty());
        int previousIndex = -1;
        for (const DrillNote& note : melody){
            auto found = std::find_if(position.begin(), position.end(), [&](const DrillNote& p){ return p.pitch == note.pitch; });
            REQUIRE(found != position.end());               // a note of the position...
            CHECK(note.stringIndex == found->stringIndex);  // ...played where the position has it
            CHECK(note.fret == found->fret);
            int index = (int)(found - position.begin());
            if (previousIndex >= 0){
                CHECK(index != previousIndex);                   // never the same note twice in a row
                CHECK(std::abs(index - previousIndex) <= 2);     // at most a leap of two scale notes
            }
            previousIndex = index;
        }
    }
}

TEST_CASE("a position without enough of the scale is refused"){
    ReadingConfig config;
    config.strings = { 0 };
    config.lowestFret = 6; config.highestFret = 6; // one fret, one note: nothing to read
    std::mt19937 rng(1);
    std::vector<DrillNote> melody;
    std::string error;
    CHECK_FALSE(buildReading(config, rng, melody, error));
    CHECK_FALSE(error.empty());
}
