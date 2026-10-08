#include "doctest/doctest.h"

#include "core/inputs.h"

#include <vector>

TEST_CASE("one channel out of an interface's interleaved inputs, or all of them mixed"){
    // Two inputs, three frames: input 1 is 1 2 3, input 2 is 10 20 30
    const float interleaved[] = { 1, 10, 2, 20, 3, 30 };
    std::vector<float> out(3);
    takeChannel(interleaved, 3, 2, 0, out.data());
    CHECK(out == std::vector<float>{1, 2, 3});
    takeChannel(interleaved, 3, 2, 1, out.data());
    CHECK(out == std::vector<float>{10, 20, 30});
    takeChannel(interleaved, 3, 2, -1, out.data());   // mixed: the average
    CHECK(out == std::vector<float>{5.5f, 11, 16.5f});
    takeChannel(interleaved, 3, 2, 7, out.data());    // a channel the device doesn't have: mixed too
    CHECK(out[0] == doctest::Approx(5.5f));
    const float mono[] = { 4, 5, 6 };
    takeChannel(mono, 3, 1, 0, out.data());
    CHECK(out == std::vector<float>{4, 5, 6});
}

TEST_CASE("an input switched off is left out: of the mix, and silent when it's the one asked for"){
    // Three inputs: a mic on 1 (100s), a bass on 2 (1 2), a guitar on 3 (10 20); the mic switched off
    const float interleaved[] = { 100, 1, 10, 200, 2, 20 };
    const uint64_t off = inputsOffMask({ 0 });
    CHECK(off == 1u);
    std::vector<float> out(2);
    takeChannel(interleaved, 2, 3, -1, out.data(), off); // mixed: the two left, their average
    CHECK(out == std::vector<float>{5.5f, 11});
    takeChannel(interleaved, 2, 3, 1, out.data(), off);  // the bass: as it was
    CHECK(out == std::vector<float>{1, 2});
    takeChannel(interleaved, 2, 3, 0, out.data(), off);  // the mic itself: nothing
    CHECK(out == std::vector<float>{0, 0});
    takeChannel(interleaved, 2, 3, -1, out.data(), inputsOffMask({ 0, 1, 2 })); // every one off: silence, not a division by 0
    CHECK(out == std::vector<float>{0, 0});
    const float mono[] = { 4, 5 };
    takeChannel(mono, 2, 1, -1, out.data(), off);        // a device with one input, switched off
    CHECK(out == std::vector<float>{0, 0});
    CHECK(inputsOffMask({ 70, -1 }) == 0u);              // inputs no mask can hold: ignored
}

TEST_CASE("an instrument told by its lowest open string"){
    CHECK(guessInstrument(41.2f) == "a bass");        // E1
    CHECK(guessInstrument(82.4f) == "a guitar, or a low voice"); // E2
    CHECK(guessInstrument(73.4f) == "a guitar, or a low voice"); // drop D
    CHECK(guessInstrument(98.0f) == "a guitar, or a low voice"); // G2: a man singing low
    CHECK(guessInstrument(220.0f) == "a voice, or a higher instrument");
    CHECK(guessInstrument(0.0f) == "nothing yet");

    CHECK(fitsRole(InputRole::Bass, 41.2f));
    CHECK_FALSE(fitsRole(InputRole::Bass, 82.4f));    // a guitar plugged into the bass's input
    CHECK(fitsRole(InputRole::Guitar, 82.4f));
    CHECK_FALSE(fitsRole(InputRole::Guitar, 41.2f));  // a bass on the guitar's
    CHECK(fitsRole(InputRole::Voice, 196.0f));
    CHECK(fitsRole(InputRole::Guitar, 0.0f));         // nothing heard yet: no complaint

    CHECK(roleForTuning(28) == InputRole::Bass);      // a 4-string bass's E1
    CHECK(roleForTuning(23) == InputRole::Bass);      // a 5-string's B0
    CHECK(roleForTuning(40) == InputRole::Guitar);    // E2
    CHECK(roleForTuning(35) == InputRole::Bass);      // a 7-string's B1 is bass territory too
}

TEST_CASE("an input is played when it rises well above its own floor, however quiet it is"){
    NoiseFloor mic, bass;
    // The mic hears the room at -50 dB; the instrument input is near silent at -85 dB
    for (int i = 0; i < 60; i++){
        trackNoiseFloor(mic, -50.0f, 1.0f / 60);
        trackNoiseFloor(bass, -85.0f, 1.0f / 60);
    }
    // A bass note at -45 dB: quieter than a loud voice, but 40 dB above its input's floor
    CHECK(isSounding(bass, -45.0f));
    // The same -45 dB on the mic is just the room getting a little louder
    CHECK_FALSE(isSounding(mic, -45.0f));
    CHECK(isSounding(mic, -25.0f));
    CHECK(riseAboveFloor(bass, -45.0f) == doctest::Approx(40.0f));
    // Below any level playing could reach, nothing counts
    CHECK_FALSE(isSounding(bass, -80.0f));
}

TEST_CASE("the noise floor follows the quietest level, and rises slowly"){
    NoiseFloor floor;
    trackNoiseFloor(floor, -40.0f, 0.1f);
    CHECK(floor.db == doctest::Approx(-40.0f));
    trackNoiseFloor(floor, -60.0f, 0.1f);                   // quieter: at once
    CHECK(floor.db == doctest::Approx(-60.0f));
    for (int i = 0; i < 10; i++) trackNoiseFloor(floor, -50.0f, 0.1f); // a second of quiet hum, not played
    CHECK(floor.db == doctest::Approx(-57.0f));             // 3 dB a second
}

TEST_CASE("a long note stays played: the floor hardly rises under it"){
    // As measured on a Scarlett Solo's instrument input: hiss near -100 dB, then a bass note ringing out over ten
    // seconds, from -10 dB down to -45 dB
    NoiseFloor floor;
    for (int i = 0; i < 60; i++) trackNoiseFloor(floor, -100.0f, 1.0f / 60);
    for (int i = 0; i <= 600; i++){
        float level = -10.0f - 35.0f * i / 600.0f;
        trackNoiseFloor(floor, level, 1.0f / 60);
        CHECK(isSounding(floor, level));
    }
}

TEST_CASE("exact digital silence doesn't drag the floor down"){
    NoiseFloor floor;
    trackNoiseFloor(floor, -120.0f, 0.1f); // exact zeros, between notes
    CHECK(floor.db == doctest::Approx(-100.0f));
    CHECK_FALSE(isSounding(floor, -95.0f)); // hiss coming back isn't playing
}

TEST_CASE("the input played is the one that rose most, bleed and all"){
    // As measured on a Scarlett Solo: singing, the mic rose 47 dB above its floor, and the bass's input 37 dB (its
    // strings ringing along). Playing the bass, only its input rose.
    CHECK(playedInput({47.0f, 37.0f}, {}) == 0);
    CHECK(playedInput({0.0f, 45.0f}, {}) == 1);
    CHECK(playedInput({0.0f, 0.0f}, {}) == -1);
    // The bass already on input 2: finding the voice, a slightly louder bleed there doesn't win
    CHECK(playedInput({30.0f, 40.0f}, {false, true}) == 0);
    // But roles set wrong (the voice on the bass's input) can still be put right: the bass rises far more
    CHECK(playedInput({10.0f, 90.0f}, {false, true}) == 1);
    CHECK(playedInput({0.0f, 40.0f}, {false, true}) == 1); // the only one sounding
}
