#include "views/tab.h"

#include "views/viewfont.h"

#include <algorithm>

const float MAX_LINE_SPACING = 26.0f;    // past this the tab only gets bigger, not clearer
const float NUMBER_SIZE = 1.3f;          // font size, in line spacings: a digit is about 0.7 of it, so it nearly fills the gap between two lines
const float CLEF_AREA_WIDTH = 2.6f;      // in line spacings: notes that have passed the hit line slide under the clef
const float LINE_THICKNESS = 1.2f;

void drawTab(Rectangle area, const std::vector<PlayNote>& notes, const Score& score, int stringCount, const TimeAxis& axis){
    if (stringCount < 1) return;
    const float spacing = std::min(MAX_LINE_SPACING, area.height / (stringCount + 1));
    const float topLineY = area.y + (area.height - spacing * (stringCount - 1)) / 2;
    const float bottomLineY = topLineY + spacing * (stringCount - 1);
    auto lineY = [&](int stringIndex){ return topLineY + (stringCount - 1 - stringIndex) * spacing; }; // string 0, the lowest, at the bottom
    const float numberSize = NUMBER_SIZE * spacing;
    const float right = area.x + area.width;

    BeginScissorMode((int)area.x, (int)area.y, (int)area.width, (int)area.height);
    DrawRectangleRec(area, themeColor(UiColor::Card));
    auto drawLines = [&](float fromX, float toX){
        for (int s = 0; s < stringCount; s++) DrawLineEx({fromX, lineY(s)}, {toX, lineY(s)}, LINE_THICKNESS, themeColor(UiColor::Ink));
    };
    drawLines(area.x, right);
    for (const ScoreBar& bar : score.bars){
        float x = axis.xAt(bar.time) - axis.barLineGap;
        if (x >= area.x && x <= right) DrawLineEx({x, topLineY}, {x, bottomLineY}, LINE_THICKNESS * 1.4f, themeColor(UiColor::Ink));
    }
    DrawLineEx({axis.hitLineX, topLineY - spacing}, {axis.hitLineX, bottomLineY + spacing}, 2.0f, themeColor(UiColor::Accent));

    float firstVisibleTime = axis.timeAt(area.x - 2 * spacing);
    auto it = std::lower_bound(notes.begin(), notes.end(), firstVisibleTime,
                               [](const PlayNote& note, float time){ return note.time < time; });
    for (; it != notes.end(); ++it){
        const PlayNote& note = *it;
        float x = axis.xAt(note.time);
        if (x > right + 2 * spacing) break;
        if (note.stringIndex < 0 || note.stringIndex >= stringCount) continue;
        float y = lineY(note.stringIndex);
        const char* fret = TextFormat("%d", note.fret);
        // The line is broken behind the number, as in printed tab, so a 1 isn't mistaken for part of the line
        float width = viewTextWidth(fret, numberSize);
        DrawRectangleRec({x - width / 2 - 0.15f * spacing, y - 0.45f * spacing, width + 0.3f * spacing, 0.9f * spacing}, themeColor(UiColor::Card));
        Color color = note.hitFlash > 0.0f ? (note.wasPerfect ? themeColor(UiColor::Good) : themeColor(UiColor::Accent)) : themeColor(UiColor::Ink);
        drawViewText(fret, x, y, numberSize, color);
    }

    // The clef on its own strip of paper: T, A and B stacked down the lines
    float clefRight = area.x + CLEF_AREA_WIDTH * spacing;
    DrawRectangleRec({area.x, area.y, clefRight - area.x, area.height}, themeColor(UiColor::Card));
    drawLines(area.x, clefRight);
    float letterSize = std::min(1.4f * spacing, (bottomLineY - topLineY + spacing) / 3);
    float clefX = area.x + CLEF_AREA_WIDTH * spacing / 2;
    float middle = (topLineY + bottomLineY) / 2;
    const char* letters[] = { "T", "A", "B" };
    for (int i = 0; i < 3; i++){
        float y = middle + (i - 1) * letterSize;
        DrawRectangleRec({clefX - 0.45f * letterSize, y - 0.42f * letterSize, 0.9f * letterSize, 0.84f * letterSize}, themeColor(UiColor::Card));
        drawViewText(letters[i], clefX, y, letterSize, themeColor(UiColor::Ink));
    }
    EndScissorMode();
}
