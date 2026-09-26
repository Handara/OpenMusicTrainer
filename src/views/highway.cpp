#include "views/highway.h"

#include "core/music.h"

#include <algorithm>

const float MAX_LANE_SPACING = 70.0f;
const Color TRACK_PANEL = { 30, 18, 12, 200 };
const Color LANE_COLORS[] = { RED, ORANGE, GOLD, GREEN, SKYBLUE, PURPLE };

void drawHighway(Rectangle area, const std::vector<PlayNote>& notes, const std::vector<int>& tuning,
                 bool lowStringOnTop, const TimeAxis& axis){
    const int laneCount = (int)tuning.size();
    const float spacing = std::min(MAX_LANE_SPACING, area.height / laneCount);
    const float scale = spacing / MAX_LANE_SPACING; // shrink notes and text along with the lanes
    const float firstLaneY = area.y + (area.height - spacing * (laneCount - 1)) / 2;
    // Which row each string gets depends on the string order setting, decided here and nowhere else
    auto laneY = [&](int stringIndex){
        int row = lowStringOnTop ? stringIndex : laneCount - 1 - stringIndex;
        return firstLaneY + row * spacing;
    };
    const float noteRadius = 16.0f * scale;
    const float targetRadius = 22.0f * scale;
    const int fontSize = std::max(10, (int)(20 * scale));

    DrawRectangleRec(area, TRACK_PANEL);
    for (int i = 0; i < laneCount; i++){
        float y = laneY(i);
        DrawLineV({area.x, y}, {area.x + area.width, y}, Fade(WHITE, 0.15f));
        DrawCircleLinesV({axis.hitLineX, y}, targetRadius, Fade(LANE_COLORS[i % 6], 0.8f));
        const char* label = TextFormat("%s%d [%d]", pitchClassName(tuning[i]), pitchOctave(tuning[i]), i + 1);
        DrawText(label, (int)area.x + 10, (int)(y - fontSize / 2), fontSize, RAYWHITE);
    }
    DrawLineV({axis.hitLineX, area.y}, {axis.hitLineX, area.y + area.height}, GOLD);

    // Notes are sorted by time: skip straight to the first one still on screen, stop after the last
    float firstVisibleTime = axis.timeAt(area.x - targetRadius * 3);
    auto it = std::lower_bound(notes.begin(), notes.end(), firstVisibleTime,
                               [](const PlayNote& note, float time){ return note.time < time; });
    for (; it != notes.end(); ++it){
        const PlayNote& note = *it;
        float x = axis.xAt(note.time);
        if (x > area.x + area.width + noteRadius) break;
        float y = laneY(note.stringIndex);
        Color color = LANE_COLORS[note.stringIndex % 6];
        if (note.hitFlash > 0.0f){
            float t = note.hitFlash / HIT_FLASH_DURATION;
            Color ringColor = note.wasPerfect ? WHITE : YELLOW;
            DrawCircleLinesV({x, y}, targetRadius + (1.0f - t) * 20 * scale, Fade(ringColor, t));
            color = ringColor;
        }
        DrawCircleV({x, y}, noteRadius, color);
        DrawCircleLinesV({x, y}, noteRadius, RAYWHITE);
        const char* fretText = TextFormat("%d", note.fret);
        DrawText(fretText, (int)(x - MeasureText(fretText, fontSize) / 2), (int)(y - fontSize / 2), fontSize, BLACK);
    }
}
