#include "core/calibration.h"

#include <algorithm>
#include <cmath>

static double median(std::vector<double> values){
    std::sort(values.begin(), values.end());
    size_t n = values.size();
    return n % 2 ? values[n / 2] : (values[n / 2 - 1] + values[n / 2]) / 2.0;
}

OffsetEstimate estimateOffset(std::vector<double> differences, int minimumCount){
    OffsetEstimate estimate;
    differences.erase(std::remove_if(differences.begin(), differences.end(),
                                     [](double d){ return std::fabs(d) > CALIBRATION_MAX_DIFFERENCE_S; }),
                      differences.end());
    estimate.count = (int)differences.size();
    if (estimate.count < minimumCount) return estimate;

    estimate.offset = median(differences);
    std::vector<double> distances;
    for (double d : differences) distances.push_back(std::fabs(d - estimate.offset));
    estimate.spread = median(distances);
    estimate.valid = true;
    return estimate;
}
