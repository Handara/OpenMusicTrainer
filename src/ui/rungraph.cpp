#include "ui/rungraph.h"

#include "raylib.h"

#include <algorithm>
#include <cmath>

UiColor gradeColor(Grade grade){
    switch (grade){
        case Grade::SS: case Grade::S: return UiColor::Accent; // brass: the best there is
        case Grade::A:                 return UiColor::Good;
        case Grade::D:                 return UiColor::Bad;
        default:                       return UiColor::Ink;
    }
}

void drawRunHistory(ImDrawList* draw, const std::vector<RunRecord>& runs, ImVec2 at, ImVec2 size, float s, const RunGraphLook& look){
    if (runs.empty() || size.x <= 0 || size.y <= 0) return;
    const UiFonts& fonts = uiFonts();
    const int count = (int)runs.size();
    int lowest = runs[0].score, highest = runs[0].score, best = 0;
    for (int i = 0; i < count; i++){
        lowest = std::min(lowest, runs[i].score);
        if (runs[i].score > highest){ highest = runs[i].score; best = i; }
    }

    // The scores fill the height, with a margin so the points aren't cut at the edges; all equal, they sit mid-way
    const float margin = (look.labels ? 7.0f : 3.0f) * s;
    float spread = (float)(highest - lowest);
    auto pointOf = [&](int i){
        float x = count == 1 ? at.x + size.x / 2 : at.x + margin + (size.x - 2 * margin) * i / (count - 1);
        float share = spread > 0 ? (runs[i].score - lowest) / spread : 0.5f;
        return ImVec2(x, at.y + size.y - margin - (size.y - 2 * margin) * share);
    };
    const float reveal = std::clamp(look.reveal, 0.0f, 1.0f);
    draw->PushClipRect(ImVec2(at.x - 8 * s, at.y - 8 * s), ImVec2(at.x + (size.x + 8 * s) * reveal, at.y + size.y + 8 * s), true);

    // The best score, a faint line across
    if (look.labels && count > 1){
        float y = pointOf(best).y;
        for (float x = at.x; x < at.x + size.x; x += 8 * s){
            draw->AddLine(ImVec2(x, y), ImVec2(std::min(x + 4 * s, at.x + size.x), y), uiColor(UiColor::Accent, 0.45f), std::max(1.0f, s));
        }
    }
    // The area under the line, then the line
    std::vector<ImVec2> points;
    points.reserve(count);
    for (int i = 0; i < count; i++) points.push_back(pointOf(i));
    const float floor = at.y + size.y;
    for (int i = 0; i + 1 < count; i++){
        draw->AddQuadFilled(points[i], points[i + 1], ImVec2(points[i + 1].x, floor), ImVec2(points[i].x, floor), uiColor(UiColor::Ink, 0.05f));
    }
    if (count > 1) draw->AddPolyline(points.data(), count, uiColor(UiColor::Ink, look.labels ? 0.45f : 0.35f), ImDrawFlags_None, (look.labels ? 2.0f : 1.5f) * s);

    // The runs: a dot each while they're far enough apart to tell, in their grade's color; the marked one, and the
    // latest on the small graph, bigger
    float gap = count > 1 ? (size.x - 2 * margin) / (count - 1) : size.x;
    bool dots = gap >= 7 * s;
    for (int i = 0; i < count; i++){
        bool marked = i == look.marked, latest = !look.labels && i == count - 1;
        if (!dots && !marked && !latest) continue;
        ImU32 color = uiColor(gradeColor(runs[i].grade()));
        float radius = (look.labels ? 3.5f : 2.5f) * s;
        if (marked){
            draw->AddCircleFilled(points[i], 8 * s, uiColor(UiColor::Card), 24);
            draw->AddCircle(points[i], 8 * s, uiColor(UiColor::Accent), 24, 2.0f * s);
            radius = 4.5f * s;
        } else if (latest) radius = 3.2f * s;
        draw->AddCircleFilled(points[i], radius + 1.2f * s, uiColor(UiColor::Card), 16);
        draw->AddCircleFilled(points[i], radius, color, 16);
    }
    draw->PopClipRect();

    if (look.labels && count > 1){
        // The best score by its line, at the right; the first and the latest run under the ends
        const char* bestText = TextFormat("BEST %d", highest);
        float bestWidth = fonts.mono ? fonts.mono->CalcTextSizeA(12 * s, FLT_MAX, 0.0f, bestText).x : 60 * s;
        float bestY = pointOf(best).y - 12 * s - 5 * s;
        draw->AddText(fonts.mono, 12 * s, ImVec2(at.x + size.x - bestWidth, std::max(at.y - 16 * s, bestY)), uiColor(UiColor::Accent, reveal), bestText);
        draw->AddText(fonts.mono, 12 * s, ImVec2(at.x, at.y + size.y + 6 * s), uiColor(UiColor::Dim, reveal), "FIRST");
        const char* latestText = TextFormat("RUN %d", count);
        float latestWidth = fonts.mono ? fonts.mono->CalcTextSizeA(12 * s, FLT_MAX, 0.0f, latestText).x : 40 * s;
        draw->AddText(fonts.mono, 12 * s, ImVec2(at.x + size.x - latestWidth, at.y + size.y + 6 * s), uiColor(UiColor::Dim, reveal), latestText);
    }
}
