#pragma once

#include "core/ranking.h"
#include "imgui.h"
#include "ui/theme.h"

#include <vector>

// A part's runs as they were played, oldest on the left: each run's score a point in its grade's color, joined by a
// line, the area under it softly filled, and the best score a faint line across. The results screen shows it large,
// with this run marked; the song list shows a small one for each part.

UiColor gradeColor(Grade grade); // brass for SS and S, green for A, red for D, ink for the rest

struct RunGraphLook {
    bool labels = true;  // the best score's label and the runs' count: off for the small one
    int marked = -1;     // a run to mark (this one, on the results), -1 for none
    float reveal = 1.0f; // how much is drawn, from the left, 0 to 1: it's traced in as it appears
};

void drawRunHistory(ImDrawList* draw, const std::vector<RunRecord>& runs, ImVec2 topLeft, ImVec2 size, float scale,
                    const RunGraphLook& look = {});
