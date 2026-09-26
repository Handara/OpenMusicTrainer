#include "doctest/doctest.h"

#include "core/calibration.h"

TEST_CASE("the offset is the typical difference, and a stray tap can't pull it"){
    std::vector<double> taps = {0.030, 0.042, 0.035, 0.038, 0.031, 0.040, 0.036, 0.033, 0.039, 0.037};
    OffsetEstimate steady = estimateOffset(taps);
    REQUIRE(steady.valid);
    CHECK(steady.offset == doctest::Approx(0.0365));
    CHECK(steady.spread < 0.005);

    taps.push_back(0.25); // one badly late tap
    CHECK(estimateOffset(taps).offset == doctest::Approx(0.037)); // the median barely moves (a mean would jump ~20 ms)
}

TEST_CASE("mistakes are left out, and too few taps give no result"){
    std::vector<double> taps = {0.02, 0.02, 0.02, 0.5, -0.6, 0.02}; // two taps nowhere near a beat
    OffsetEstimate estimate = estimateOffset(taps, 4);
    CHECK(estimate.count == 4);
    CHECK(estimate.valid);
    CHECK_FALSE(estimateOffset({0.02, 0.03}).valid); // 2 taps: not enough to trust
}

TEST_CASE("early is negative"){
    OffsetEstimate estimate = estimateOffset({-0.01, -0.012, -0.011, -0.009, -0.01, -0.011, -0.01, -0.01});
    REQUIRE(estimate.valid);
    CHECK(estimate.offset < 0.0);
}
