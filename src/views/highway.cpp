#include "views/highway.h"

#include "core/music.h"

#include <algorithm>

const float MAX_LANE_SPACING = 70.0f;
const Color TRACK_PANEL = { 30, 18, 12, 200 };
const Color LANE_COLORS[] = { RED, ORANGE, GOLD, GREEN, SKYBLUE, PURPLE };

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
    const int fontSize = std::max(10, (int)(20 * scale));
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

    DrawRectangleRec(area, TRACK_PANEL);
    BeginScissorMode((int)area.x, (int)area.y, (int)area.width, (int)area.height);
    const float acrossStart = firstLane - spacing / 2, acrossEnd = firstLane + spacing * (laneCount - 0.5f);

    // Bar lines, faint, right on each bar's first beat: a note on the line is on the downbeat
    for (const ScoreBar& bar : score.bars){
        float along = alongAt(bar.time);
        if (along < alongStart || along > alongEnd) continue;
        DrawLineEx(point(along, acrossStart), point(along, acrossEnd), 2.0f, Fade(WHITE, 0.12f));
    }

    for (int i = 0; i < laneCount; i++){
        float across = laneAt(i);
        DrawLineEx(point(alongStart, across), point(alongEnd, across), 1.0f, Fade(WHITE, 0.15f));
        DrawCircleLinesV(point(hitAlong, across), targetRadius, Fade(LANE_COLORS[i % 6], 0.8f));
        const char* label = TextFormat("%s%d [%d]", pitchClassName(tuning[i]), pitchOctave(tuning[i]), i + 1);
        if (falls){
            // Under each column's target, centered on it
            DrawText(label, (int)(across - MeasureText(label, fontSize) / 2), (int)(hitAlong + targetRadius + 8), fontSize, RAYWHITE);
        } else {
            DrawText(label, (int)area.x + 10, (int)(across - fontSize / 2), fontSize, RAYWHITE);
        }
    }
    DrawLineEx(point(hitAlong, acrossStart), point(hitAlong, acrossEnd), 2.0f, GOLD);

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
        Color color = LANE_COLORS[note.stringIndex % 6];
        if (note.hitFlash > 0.0f){
            float t = note.hitFlash / HIT_FLASH_DURATION;
            Color ringColor = note.wasPerfect ? WHITE : YELLOW;
            DrawCircleLinesV(center, targetRadius + (1.0f - t) * 20 * scale, Fade(ringColor, t));
            color = ringColor;
        }
        DrawCircleV(center, noteRadius, color);
        DrawCircleLinesV(center, noteRadius, RAYWHITE);
        const char* fretText = TextFormat("%d", note.fret);
        DrawText(fretText, (int)(center.x - MeasureText(fretText, fontSize) / 2), (int)(center.y - fontSize / 2), fontSize, BLACK);
    }
    EndScissorMode();
}
