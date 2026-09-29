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

TEST_CASE("an instrument told by its lowest open string"){
    CHECK(guessInstrument(41.2f) == "a bass");        // E1
    CHECK(guessInstrument(82.4f) == "a guitar");      // E2
    CHECK(guessInstrument(73.4f) == "a guitar");      // drop D
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
