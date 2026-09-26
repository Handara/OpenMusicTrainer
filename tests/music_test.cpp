#include "doctest/doctest.h"

#include "core/music.h"

#include <string>

TEST_CASE("note names and octaves"){
    CHECK(std::string(pitchClassName(60)) == "C");
    CHECK(pitchOctave(60) == 4);  // middle C
    CHECK(std::string(pitchClassName(40)) == "E");
    CHECK(pitchOctave(40) == 2);  // guitar low E
    CHECK(std::string(pitchClassName(70)) == "A#");
    CHECK(std::string(pitchClassName(11)) == "B");
    CHECK(pitchOctave(11) == -1); // lowest MIDI octave
}

TEST_CASE("frequency and MIDI pitch convert both ways"){
    CHECK(frequencyToMidi(440.0f) == doctest::Approx(69.0f));
    CHECK(frequencyToMidi(880.0f) == doctest::Approx(81.0f));      // one octave = 12 semitones
    CHECK(frequencyToMidi(82.4069f) == doctest::Approx(40.0f).epsilon(0.0001));
    CHECK(midiToFrequency(69.0f) == doctest::Approx(440.0f));
    CHECK(midiToFrequency(frequencyToMidi(123.4f)) == doctest::Approx(123.4f));
    CHECK(frequencyToMidi(440.0f * 1.0145453f) == doctest::Approx(69.25f).epsilon(0.001)); // +25 cents
}
