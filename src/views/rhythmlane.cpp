#include "views/rhythmlane.h"

#include "ui/theme.h"

#include <algorithm>

const Color KA_BLUE = { 50, 120, 190, 255 };  // the highway's B string blue: the one cool colour next to the brass
const float LANE_HEIGHT = 120.0f;             // at scale 1 (a 450-pixel-tall area)
const float NOTE_RADIUS = 26.0f;
const float BIG_RADIUS = 38.0f;

void drawRhythmLane(Rectangle area, const std::vector<PlayNote>& notes, const Score& score, const TimeAxis& axis){
    const float s = std::clamp(area.height / 450.0f, 0.6f, 2.0f);
    const float laneHeight = LANE_HEIGHT * s, middle = area.y + area.height / 2;
    const Color card = themeColor(UiColor::Card), line = themeColor(UiColor::StaffLine), accent = themeColor(UiColor::Accent);
    auto colorOf = [&](const PlayNote& note){ return note.stringIndex == 1 ? KA_BLUE : accent; };
    auto radiusOf = [&](const PlayNote& note){ return (note.fret == 1 ? BIG_RADIUS : NOTE_RADIUS) * s; };

    DrawRectangleRec({area.x, middle - laneHeight / 2, area.width, laneHeight}, card);
    // Bar lines, faint, across the lane: the measure to read the rhythm by
    for (const ScoreBar& bar : score.bars){
        float x = axis.xAt(bar.time);
        if (x < area.x || x > area.x + area.width) continue;
        DrawRectangleRec({x - 1.0f, middle - laneHeight / 2, 2.0f, laneHeight}, line);
    }
    // The target: where a hit lands
    Vector2 target = { axis.hitLineX, middle };
    DrawRing(target, NOTE_RADIUS * s + 4 * s, NOTE_RADIUS * s + 7 * s, 0.0f, 360.0f, 64, line);
    DrawCircleV(target, NOTE_RADIUS * s * 0.55f, Fade(line, 0.7f));

    // The notes, the latest drawn first so the next one to hit is always on top
    BeginScissorMode((int)area.x, (int)(middle - laneHeight / 2 - BIG_RADIUS * s), (int)area.width, (int)(laneHeight + 2 * BIG_RADIUS * s));
    float earliest = axis.timeAt(area.x - BIG_RADIUS * s), latest = axis.timeAt(area.x + area.width + BIG_RADIUS * s);
    auto first = std::lower_bound(notes.begin(), notes.end(), earliest, [](const PlayNote& note, float time){ return note.time < time; });
    auto last = std::upper_bound(first, notes.end(), latest, [](float time, const PlayNote& note){ return time < note.time; });
    for (auto it = last; it != first; ){
        const PlayNote& note = *--it;
        float radius = radiusOf(note);
        if (note.hit){
            // A hit is gone: it bursts at the target, green for perfect or brass for good, and fades
            if (note.hitFlash <= 0.0f) continue;
            float t = note.hitFlash / HIT_FLASH_DURATION;
            Color lit = themeColor(note.wasPerfect ? UiColor::Good : UiColor::Accent);
            float grown = radius + (1.0f - t) * 26 * s;
            DrawCircleV(target, radius * (0.7f + 0.3f * t), Fade(colorOf(note), t));
            DrawRing(target, grown - 3 * s, grown, 0.0f, 360.0f, 64, Fade(lit, t));
            continue;
        }
        Vector2 center = { axis.xAt(note.time), middle };
        float alpha = note.judged ? 0.3f : 1.0f; // a missed hit: its ghost
        DrawCircleV(center, radius + 3 * s, Fade(card, alpha));
        DrawCircleV(center, radius, Fade(colorOf(note), alpha));
        DrawCircleV(center, radius * 0.38f, Fade(ColorBrightness(colorOf(note), 0.35f), alpha)); // a lighter centre, like a drum head
    }
    EndScissorMode();
}
