#include "doctest/doctest.h"

#include "core/backing.h"
#include "core/notedetector.h"

#include <string>
#include <vector>

// Recordings of a real bass (a Scarlett Solo's instrument input), made with the Instrument screen's "Record a check"
// on 2026-10-03, where hardthz heard notes that weren't played. tests/data/bass-line.wav: a line around E2 to B2, the
// fretting hand often on the next note before it's plucked. tests/data/bass-octaves.wav: B1 and B2 in turn, the
// first muted as the second is played.

struct Heard { double at; int pitch; };

// The notes found, as the game hears them: a frame's worth of samples at a time, down to a bass's low E
static std::vector<Heard> hear(const char* file){
    std::vector<float> samples;
    int rate = 0;
    std::string error;
    REQUIRE_MESSAGE(readWav(std::string(HARDTHZ_TEST_DATA_DIR) + file, samples, rate, error), error);
    NoteDetectorConfig config;
    config.minFrequency = 37.0f;
    NoteDetector detector;
    initNoteDetector(detector, rate, config);
    std::vector<DetectedNote> found;
    for (size_t at = 0; at < samples.size(); at += 735) feedNoteDetector(detector, samples.data() + at, (int)std::min<size_t>(735, samples.size() - at), found);
    std::vector<Heard> heard;
    for (const DetectedNote& note : found) heard.push_back({ note.sample / (double)rate, note.pitch });
    return heard;
}

static std::vector<Heard> between(const std::vector<Heard>& heard, double from, double to){
    std::vector<Heard> some;
    for (const Heard& note : heard) if (note.at >= from && note.at < to) some.push_back(note);
    return some;
}

TEST_CASE("a real bass: the fretting hand on the next note before it's plucked isn't a note"){
    std::vector<Heard> heard = hear("bass-line.wav");
    // E2, then F#2 fretted while the E2 rang (4.25 s), plucked at 4.38: one F#2, at its pluck
    std::vector<Heard> fSharp = between(heard, 4.1, 4.6);
    REQUIRE(fSharp.size() == 1);
    CHECK(fSharp[0].pitch == 42);
    CHECK(fSharp[0].at == doctest::Approx(4.38).epsilon(0.005));
    // The same, before the plucks at 8.97, 9.47 and 11.83
    CHECK(between(heard, 8.6, 8.95).empty());
    CHECK(between(heard, 9.2, 9.45).empty());
    CHECK(between(heard, 11.5, 11.8).empty());
    // The plucked notes are all there: the B1s at the start (below a guitar's range), and the run in the middle
    REQUIRE(between(heard, 1.6, 2.1).size() == 2);
    CHECK(between(heard, 1.6, 2.1)[0].pitch == 35);
    std::vector<int> run;
    for (const Heard& note : between(heard, 5.5, 7.1)) run.push_back(note.pitch);
    CHECK(run == std::vector<int>{ 45, 47, 42, 45, 47, 47 }); // A2 B2 F#2 A2 B2 B2
}

TEST_CASE("a real bass: muting a note isn't a note, nor a finger touching a string that rings"){
    std::vector<Heard> heard = hear("bass-octaves.wav");
    // B1 muted as the hand goes to the B2 (16.96, 22.92 and 25.33 s in the recording, 14.9 s cut from its start): it
    // read as an E1. Only the B2 plucked after it is heard
    CHECK(between(heard, 1.9, 2.2).size() == 1);
    CHECK(between(heard, 7.9, 8.15).size() == 1);
    CHECK(between(heard, 10.3, 10.55).size() == 1);
    for (const Heard& note : heard) CHECK(note.pitch != 28);
    // A click on the ringing B2 70 ms before it's plucked again: one B2, at the pluck
    std::vector<Heard> again = between(heard, 2.5, 2.8);
    REQUIRE(again.size() == 1);
    CHECK(again[0].pitch == 47);
    CHECK(again[0].at == doctest::Approx(2.68).epsilon(0.01));
}

TEST_CASE("a real bass: hammer-ons and pull-offs are notes, in the octave they're played"){
    // tests/data/bass-legato.wav: plucked, then hammered on or pulled off a fret or two, the next pluck well after
    // (1.2 s cut from the recording's start). An open string rang along in sympathy, and made the A2s and the F2 read
    // an octave low before
    std::vector<Heard> heard = hear("bass-legato.wav");
    std::vector<int> pitches;
    for (const Heard& note : heard) pitches.push_back(note.pitch);
    // B1 C#2, E2 F#2, F#2 E2, C#2 B1, A2 B2, ... E2 F2, F2 E2
    const std::vector<int> played = { 35, 37, 40, 42, 42, 40, 37, 35, 45, 47 };
    REQUIRE(pitches.size() >= played.size());
    CHECK(std::vector<int>(pitches.begin(), pitches.begin() + (long)played.size()) == played);
    CHECK(std::vector<int>(pitches.end() - 4, pitches.end()) == std::vector<int>{ 40, 41, 41, 40 });
    for (int pitch : pitches) CHECK(pitch >= 35); // nothing an octave low
}
