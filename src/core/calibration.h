#pragma once

#include <vector>

// Latency calibration: from "when each beat was due" vs "when the player's tap or note landed",
// the delay to compensate.

const double CALIBRATION_MAX_DIFFERENCE_S = 0.3; // further off than this, a tap is a mistake, not latency

struct OffsetEstimate {
    bool valid = false;  // enough good taps to trust
    double offset = 0.0; // the typical difference, seconds: positive = the player lands late
    double spread = 0.0; // how steady they were: the typical distance from that offset
    int count = 0;       // taps used
};

// Median rather than mean: one stray tap can't drag it. The spread is the median distance from the median.
OffsetEstimate estimateOffset(std::vector<double> differences, int minimumCount = 8);
