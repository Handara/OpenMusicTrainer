#pragma once

#include "ui/theme.h"

#include <string>
#include <vector>

// The exercises' scoreboard: what counts (a streak, the time, the mistakes, a best), as tiles in a row at the top of
// the screen, big enough to follow from the corner of an eye while playing. A tile whose value changes pulses: it
// swells a little and glows in its color, settling in a moment, so a streak going up or a mistake landing is seen.

struct ScoreTile {
    std::string label;          // "STREAK": small, above
    std::string value;          // "12": big
    UiColor color = UiColor::Ink;
    std::string sub;            // under it, small: "best 14", "of 31"; may be empty
};

// The tiles in a row ending at `right`, from `top`; returns how tall the row is
float drawScoreboard(const std::vector<ScoreTile>& tiles, float right, float top, float scale);
