#pragma once

#include "core/chart.h"

#include <vector>

// How hard a part is, and what a run of it is worth: the numbers ranked play is built on, worked out on the player's
// own machine (no server needed: a leaderboard later only compares them). Pure.
//
// Stars, the way osu! rates a map: every note adds strain, more for notes closer together, a hand moving further, a
// chord, a stretch; strain fades between notes. The hardest stretches of the part (its sections' peaks, the hardest
// counting most) give its rating. About 1 for quarter notes at 90 bpm on one string, 3 for eighths at 120 moving
// about, 6 and up for sixteenths at 140 jumping across the neck.
//
// Performance points (pp): a run's worth, from the part's stars and how well it went (accuracy above all, misses,
// the part's length). A player's total adds each part's best, the best first, each worth 95% of the one before (as
// osu! does), so many good runs count and one lucky one can't carry it.

// A part's rating: its fretted tracks first, then its keys tracks (as partFingerprint counts them); 0 with no notes
float partStars(const Chart& chart, int part);

// What a run's worth. Only runs on the instrument count (on the computer keyboard: 0, it isn't the instrument).
float runPerformance(float stars, float accuracy, int misses, int notes, bool withInstrument = true);

// The total of a player's best runs, one per part: the best first, each worth 95% of the one before
float totalPerformance(std::vector<float> bests);

// The weight the n-th best run (0 the best) carries in the total
float performanceWeight(int place);
