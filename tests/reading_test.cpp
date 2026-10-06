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

TEST_CASE("reading just the notes asked: at random, each where it's lowest, no long runs"){
    ReadingConfig config;
    config.pool = { 64, 65 };   // E4 and F4
    config.cells = { "quarter" };
    config.bars = 4;
    std::vector<DrillNote> notes;
    std::string error;
    int turns = 0;
    for (unsigned seed = 1; seed <= 50; seed++){
        std::mt19937 rng(seed);
        REQUIRE_MESSAGE(buildReading(config, rng, notes, error), error);
        REQUIRE(notes.size() == 16);
        bool alternates = true;
        int run = 1;
        for (size_t i = 0; i < notes.size(); i++){
            CHECK((notes[i].pitch == 64 || notes[i].pitch == 65));
            CHECK(notes[i].stringIndex == 5); // the high E string, frets 0 and 1
            CHECK(notes[i].fret == notes[i].pitch - 64);
            if (i == 0) continue;
            run = notes[i].pitch == notes[i - 1].pitch ? run + 1 : 1;
            if (run > 1) alternates = false;
            CHECK(run <= 3);
        }
        if (alternates) turns++;
    }
    CHECK(turns < 3); // not just E F E F
    // Nine notes in twelve: every one read at least once, still no long runs
    config.pool = { 52, 53, 55, 57, 59, 60, 62, 64, 65 };
    config.bars = 3;
    for (unsigned seed = 1; seed <= 50; seed++){
        std::mt19937 rng(seed);
        REQUIRE_MESSAGE(buildReading(config, rng, notes, error), error);
        REQUIRE(notes.size() == 12);
        for (int pitch : config.pool)
            CHECK(std::any_of(notes.begin(), notes.end(), [&](const DrillNote& note){ return note.pitch == pitch; }));
        int run = 1;
        for (size_t i = 1; i < notes.size(); i++){
            run = notes[i].pitch == notes[i - 1].pitch ? run + 1 : 1;
            CHECK(run <= 3);
        }
    }
    // A note off the neck is refused
    config.pool = { 20 };
    std::mt19937 rng(1);
    CHECK_FALSE(buildReading(config, rng, notes, error));
}
