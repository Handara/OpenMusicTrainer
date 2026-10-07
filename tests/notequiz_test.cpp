#include "doctest/doctest.h"

#include "core/music.h"
#include "core/notequiz.h"

#include <cstdio>
#include <filesystem>
#include <vector>

static const std::vector<int> GUITAR = { 40, 45, 50, 55, 59, 64 };
static const std::vector<int> BASS = { 28, 33, 38, 43 };

TEST_CASE("note names: letter, sharp or flat, octave"){
    int pitch = 0;
    CHECK(parseNoteName("C4", pitch));
    CHECK(pitch == 60);
    CHECK(parseNoteName("F#3", pitch));
    CHECK(pitch == 54);
    CHECK(parseNoteName("Bb1", pitch));
    CHECK(pitch == 34);
    CHECK(parseNoteName("e2", pitch));
    CHECK(pitch == 40);
    CHECK_FALSE(parseNoteName("H3", pitch));
    CHECK_FALSE(parseNoteName("C", pitch));
    CHECK_FALSE(parseNoteName("C4x", pitch));
}

TEST_CASE("notes by name go where they're lowest on the neck: the open strings and first frets"){
    std::vector<NeckStep> places;
    std::string error;
    // E4 F4 G4 on the high E string; C4 on the B string's 1st fret (not the G string's 5th)
    REQUIRE_MESSAGE(placeNotes({ 64, 65, 67, 60 }, GUITAR, {}, places, error), error);
    CHECK(places[0].string == 5);
    CHECK(places[0].fret == 0);
    CHECK(places[1].fret == 1);
    CHECK(places[2].fret == 3);
    CHECK(places[3].string == 4);
    CHECK(places[3].fret == 1);
    // Only the strings allowed: C4 on the G string
    REQUIRE(placeNotes({ 60 }, GUITAR, { 3 }, places, error));
    CHECK(places[0].fret == 5);
    // Too low for the strings
    CHECK_FALSE(placeNotes({ 36 }, GUITAR, {}, places, error));
    CHECK(error.find("C2") != std::string::npos);
}

TEST_CASE("where a note is, said for someone who's never played"){
    CHECK(notePlaceText({ 64, 5, 0 }, GUITAR) == "the open high E string");
    CHECK(notePlaceText({ 41, 0, 1 }, GUITAR) == "the 1st fret of the low E string");
    CHECK(notePlaceText({ 62, 4, 3 }, GUITAR) == "the 3rd fret of the B string");
    CHECK(notePlaceText({ 57, 3, 2 }, GUITAR) == "the 2nd fret of the G string");
    CHECK(notePlaceText({ 52, 1, 12 }, BASS) == "the 12th fret of the A string");
    CHECK(notePlaceText({ 49, 1, 11 }, GUITAR) == "the 11th fret of the A string");
}

TEST_CASE("where a key is on a piano, said by middle C"){
    const std::vector<int> PIANO = { 0 };
    CHECK(notePlaceText({ 60, 0, 60 }, PIANO) == "middle C");
    CHECK(keyPlaceText(64) == "the E above middle C");
    CHECK(keyPlaceText(72) == "the C above middle C");
    CHECK(keyPlaceText(59) == "the B below middle C");
    CHECK(keyPlaceText(76) == "the high E (E5)");
    CHECK(keyPlaceText(43) == "the low G (G2)");
    CHECK(keyPlaceText(28) == "E1");
}

TEST_CASE("play this note: one at a time, the next when it's right; right the first time counts"){
    NoteQuizConfig config;
    std::string error;
    REQUIRE(placeNotes({ 64, 65 }, GUITAR, {}, config.notes, error));
    config.count = 6;
    config.pass = 5;
    std::mt19937 random(3);
    const std::vector<NeckStep> prompts = noteQuizPrompts(config, random);
    REQUIRE(prompts.size() == 6);
    NoteQuizRun run;
    startNoteQuiz(run, prompts);
    CHECK_FALSE(playNoteQuiz(run, config, prompts[0].pitch + 1)); // wrong: it stays
    CHECK(run.next == 0);
    CHECK(run.mistakes == 1);
    CHECK(playNoteQuiz(run, config, prompts[0].pitch));            // then right: on, but not first time
    for (size_t i = 1; i < prompts.size(); i++) CHECK(playNoteQuiz(run, config, prompts[i].pitch));
    CHECK(noteQuizDone(run));
    CHECK(noteQuizRight(run) == 5);
    CHECK(noteQuizPassed(run, config));
    CHECK_FALSE(playNoteQuiz(run, config, prompts[0].pitch));      // done: nothing more
    // An octave off isn't the note asked, unless any octave will do
    startNoteQuiz(run, prompts);
    CHECK_FALSE(playNoteQuiz(run, config, prompts[0].pitch + 12));
    config.anyOctave = true;
    CHECK(playNoteQuiz(run, config, prompts[1].pitch - 12) == false); // the next isn't asked yet: still the first
    CHECK(playNoteQuiz(run, config, prompts[0].pitch - 24));
}

TEST_CASE("play this note: in order, going round"){
    NoteQuizConfig config;
    std::string error;
    REQUIRE(placeNotes({ 59, 60, 62 }, GUITAR, {}, config.notes, error));
    config.inOrder = true;
    config.count = 5;
    std::mt19937 random(1);
    const std::vector<NeckStep> prompts = noteQuizPrompts(config, random);
    REQUIRE(prompts.size() == 5);
    CHECK(prompts[0].pitch == 59);
    CHECK(prompts[2].pitch == 62);
    CHECK(prompts[3].pitch == 59);
    // One note only: the same every time
    config.notes.resize(1);
    config.inOrder = false;
    for (const NeckStep& prompt : noteQuizPrompts(config, random)) CHECK(prompt.pitch == 59);
}

TEST_CASE("play this note: runs played and passed are kept"){
    const std::string path = (std::filesystem::temp_directory_path() / "hardthz-notequiz-test.txt").string();
    std::remove(path.c_str());
    CHECK(loadNoteQuizStats(path).runs == 0);
    std::string error;
    REQUIRE(saveNoteQuizStats(path, { 4, 3 }, error));
    const NoteQuizStats stats = loadNoteQuizStats(path);
    CHECK(stats.runs == 4);
    CHECK(stats.passed == 3);
    std::remove(path.c_str());
}

TEST_CASE("play this note: at random really, no pattern to follow, every note asked, no long runs"){
    NoteQuizConfig config;
    std::string error;
    REQUIRE(placeNotes({ 64, 65 }, GUITAR, {}, config.notes, error)); // E and F: they must not just take turns
    config.count = 12;
    int alternating = 0, repeats = 0;
    for (unsigned seed = 1; seed <= 200; seed++){
        std::mt19937 random(seed);
        const std::vector<NeckStep> prompts = noteQuizPrompts(config, random);
        REQUIRE(prompts.size() == 12);
        bool takesTurns = true, hasE = false, hasF = false;
        int run = 1;
        for (size_t i = 0; i < prompts.size(); i++){
            hasE = hasE || prompts[i].pitch == 64;
            hasF = hasF || prompts[i].pitch == 65;
            if (i == 0) continue;
            if (prompts[i].pitch == prompts[i - 1].pitch){
                takesTurns = false;
                repeats++;
                run++;
            } else {
                run = 1;
            }
            CHECK(run <= 3);
        }
        CHECK(hasE);
        CHECK(hasF);
        if (takesTurns) alternating++;
    }
    CHECK(alternating < 5);  // almost never E F E F...
    CHECK(repeats > 500);    // the same note again comes often
}
