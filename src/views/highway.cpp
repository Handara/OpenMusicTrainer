#include "views/highway.h"

#include "core/music.h"
#include "views/smooth.h"
#include "views/viewfont.h"

#include <algorithm>

const float MAX_LANE_SPACING = 70.0f;
// One color per string, low to high, so a string can be told at a glance: deep enough for white fret numbers, and
// the same in light and dark
const Color STRING_COLORS[] = { {200, 70, 62, 255}, {214, 120, 40, 255}, {190, 145, 30, 255},
                                {52, 140, 90, 255}, {50, 120, 190, 255}, {128, 90, 190, 255} };

Color stringColor(int stringIndex){ return STRING_COLORS[stringIndex % 6]; }

// Both directions draw the same things; only which screen axis is time and which is the strings differs. Positions
// are worked out as "along" (time) and "across" (the strings), then turned into x and y at the last moment.
void drawHighway(Rectangle area, const std::vector<PlayNote>& notes, const Score& score, const std::vector<int>& tuning,
                 bool lowStringFirst, bool falls, const TimeAxis& axis){
    const int laneCount = (int)tuning.size();
    const float acrossLength = falls ? area.width : area.height;
    const float spacing = std::min(MAX_LANE_SPACING, acrossLength / laneCount);
    const float scale = spacing / MAX_LANE_SPACING; // shrink notes and text along with the lanes
    const float noteRadius = 16.0f * scale;
    const float targetRadius = 22.0f * scale;
    const float fontSize = std::max(10.0f, 20.0f * scale);
    const float firstLane = (falls ? area.x : area.y) + (acrossLength - spacing * (laneCount - 1)) / 2;
    // Which lane each string gets depends on the string order setting, decided here and nowhere else
    auto laneAt = [&](int stringIndex){
        int lane = lowStringFirst ? stringIndex : laneCount - 1 - stringIndex;
        return firstLane + lane * spacing;
    };

    // Along the highway: scrolling across, time runs left to right past the shared hit line; falling, notes come
    // down from the top to a hit line near the bottom, with the string names under it
    const float alongStart = falls ? area.y : area.x, alongEnd = falls ? area.y + area.height : area.x + area.width;
    const float hitAlong = falls ? alongEnd - targetRadius - fontSize - 16.0f : axis.hitLineX;
    auto alongAt = [&](float time){ return falls ? hitAlong - (time - axis.songTime) * axis.noteSpeed : axis.xAt(time); };
    auto timeAt = [&](float along){ return falls ? axis.songTime + (hitAlong - along) / axis.noteSpeed : axis.timeAt(along); };
    auto point = [&](float along, float across){ return falls ? Vector2{across, along} : Vector2{along, across}; };

    const Color line = themeColor(UiColor::StaffLine);
    DrawRectangleRec(area, themeColor(UiColor::Card));
    BeginScissorMode((int)area.x, (int)area.y, (int)area.width, (int)area.height);
    const float acrossStart = firstLane - spacing / 2, acrossEnd = firstLane + spacing * (laneCount - 0.5f);

    // Bar lines, faint, right on each bar's first beat: a note on the line is on the downbeat
    for (const ScoreBar& bar : score.bars){
        float along = alongAt(bar.time);
        if (along < alongStart || along > alongEnd) continue;
        DrawLineEx(point(along, acrossStart), point(along, acrossEnd), 2.0f, line);
    }

    for (int i = 0; i < laneCount; i++){
        float across = laneAt(i);
        DrawLineEx(point(alongStart, across), point(alongEnd, across), 1.0f, line);
        smoothRing(point(hitAlong, across), targetRadius - 2.0f * scale, targetRadius, 0.0f, 360.0f, Fade(STRING_COLORS[i % 6], 0.7f));
        const char* label = TextFormat("%s%d [%d]", pitchClassName(tuning[i]), pitchOctave(tuning[i]), i + 1);
        Vector2 at = falls ? Vector2{across, hitAlong + targetRadius + 8 + fontSize / 2} // under each column's target
                           : Vector2{area.x + 10, across};                             // at the start of each lane
        drawViewText(label, at.x, at.y, fontSize, themeColor(UiColor::Dim), falls ? 0.5f : 0.0f);
    }
    DrawLineEx(point(hitAlong, acrossStart), point(hitAlong, acrossEnd), 2.0f, themeColor(UiColor::Accent));

    // Falling, a note is gone once it's past the hit line, so it never covers the string names under it
    if (falls){
        EndScissorMode();
        BeginScissorMode((int)area.x, (int)area.y, (int)area.width, (int)(hitAlong + targetRadius - area.y));
    }

    // Notes are sorted by time: skip straight to the first one still on screen, stop after the last. The earliest
    // time on screen is at the left edge scrolling across, at the bottom when falling.
    float margin = targetRadius * 3;
    float earliest = falls ? timeAt(alongEnd + margin) : timeAt(alongStart - margin);
    float latest = falls ? timeAt(alongStart - margin) : timeAt(alongEnd + margin);
    auto it = std::lower_bound(notes.begin(), notes.end(), earliest,
                               [](const PlayNote& note, float time){ return note.time < time; });
    for (; it != notes.end() && it->time <= latest; ++it){
        const PlayNote& note = *it;
        Vector2 center = point(alongAt(note.time), laneAt(note.stringIndex));
        if (note.hit){
            // A hit note is gone: it bursts at its target, in green for perfect or brass for good, and fades
            if (note.hitFlash <= 0.0f) continue;
            float t = note.hitFlash / HIT_FLASH_DURATION; // 1 at the hit, 0 when it's over
            Color lit = themeColor(note.wasPerfect ? UiColor::Good : UiColor::Accent);
            Vector2 target = point(hitAlong, laneAt(note.stringIndex));
            float radius = targetRadius + (1.0f - t) * 22 * scale;
            smoothCircle(target, noteRadius * (0.6f + 0.6f * t), Fade(lit, t));
            smoothRing(target, radius - 2.5f * scale, radius, 0.0f, 360.0f, Fade(lit, t));
            continue;
        }
        // A missed note goes on past the line, faded: what was missed stays visible for a moment, out of the way
        float alpha = note.judged ? 0.3f : 1.0f;
        smoothCircle(center, noteRadius + 1.5f * scale, Fade(themeColor(UiColor::Card), alpha)); // a rim that keeps notes apart
        smoothCircle(center, noteRadius, Fade(STRING_COLORS[note.stringIndex % 6], alpha));
        drawViewText(TextFormat("%d", note.fret), center.x, center.y, fontSize, Fade(WHITE, alpha));
    }
    EndScissorMode();
}
