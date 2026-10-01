#include "doctest/doctest.h"

#include "core/stemsplit.h"

#include <cmath>

// A model that leaves everything in: what comes out of the split is then the song itself (what of it is under
// 5.5 kHz), if the chunks, the transform there and back, and the putting together are all right
TEST_CASE("a song split with a model that keeps everything comes back whole"){
    const size_t length = (size_t)(STEM_RATE * 14.3); // more than one chunk, and not a whole number of them
    std::vector<float> left(length), right(length);
    for (size_t i = 0; i < length; i++){
        float t = (float)i / STEM_RATE;
        // Smooth, so nothing of it is above what the model sees: a low note, and a higher one swelling and fading
        left[i] = 0.3f * std::sin(2 * 3.14159265f * 55.0f * t) + 0.2f * std::sin(2 * 3.14159265f * 440.0f * t) * (0.5f + 0.5f * std::sin(2 * 3.14159265f * 2.0f * t));
        right[i] = 0.25f * std::sin(2 * 3.14159265f * 82.4f * t + 1.0f);
        // Faded in and out, as a recording is: a sound starting mid-wave is a click, which has every frequency
        float fade = std::min(1.0f, std::min(t, (float)(length - i) / STEM_RATE) / 0.2f);
        fade = 0.5f - 0.5f * std::cos(3.14159265f * fade);
        left[i] *= fade;
        right[i] *= fade;
    }
    int chunks = 0;
    StemModel keepAll = [&](const std::vector<float>& input, std::vector<float>& output){
        chunks++;
        output = input;
        return true;
    };
    std::vector<float> stemLeft, stemRight;
    std::atomic<float> progress{0.0f};
    std::atomic<bool> cancel{false};
    std::string error;
    REQUIRE_MESSAGE(splitStem(left, right, keepAll, stemLeft, stemRight, &progress, cancel, error), error);
    CHECK(chunks == 2);
    CHECK(progress.load() == doctest::Approx(1.0f));
    REQUIRE(stemLeft.size() == length);
    REQUIRE(stemRight.size() == length);
    double worst = 0.0;
    size_t worstAt = 0;
    for (size_t i = 0; i < length; i++){
        double off = std::max(std::fabs(stemLeft[i] - left[i]), std::fabs(stemRight[i] - right[i]));
        if (off > worst){ worst = off; worstAt = i; }
    }
    CAPTURE(worstAt);
    CHECK(worst < 0.01); // each channel its own, sample for sample

    // A model that keeps nothing gives silence; one that fails, or a cancel, stops the split
    StemModel keepNothing = [](const std::vector<float>&, std::vector<float>& output){ output.assign(STEM_TENSOR, 0.0f); return true; };
    REQUIRE(splitStem(left, right, keepNothing, stemLeft, stemRight, nullptr, cancel, error));
    for (size_t i = 0; i < length; i += 997) CHECK(std::fabs(stemLeft[i]) < 1e-6f);
    StemModel broken = [](const std::vector<float>&, std::vector<float>&){ return false; };
    CHECK_FALSE(splitStem(left, right, broken, stemLeft, stemRight, nullptr, cancel, error));
    cancel = true;
    CHECK_FALSE(splitStem(left, right, keepAll, stemLeft, stemRight, nullptr, cancel, error));
}
