#include "views/tab.h"

#include <algorithm>

const int FONT_LOAD_SIZE = 64;           // drawn smaller than this, through mipmaps, so it stays smooth
const float MAX_LINE_SPACING = 26.0f;    // past this the tab only gets bigger, not clearer
const float NUMBER_SIZE = 1.3f;          // font size, in line spacings: a digit is about 0.7 of it, so it nearly fills the gap between two lines
const float CLEF_AREA_WIDTH = 2.6f;      // in line spacings: notes that have passed the hit line slide under the clef
const float LINE_THICKNESS = 1.2f;

static struct {
    Font font;
    bool loaded = false;
    float digitCenter = 0.5f; // where a digit's middle sits in its text line, as a fraction of the font size
} tab;

bool loadTabFont(const std::string& path){
    const char* characters = "0123456789TAB";
    int count = 0;
    int* codepoints = LoadCodepoints(characters, &count);
    Font font = LoadFontEx(path.c_str(), FONT_LOAD_SIZE, codepoints, count);
    UnloadCodepoints(codepoints);
    if (font.texture.id == 0 || font.texture.id == GetFontDefault().texture.id) return false; // raylib fell back to its own font
    GenTextureMipmaps(&font.texture);                          // smaller copies of the glyphs, for drawing small
    SetTextureFilter(font.texture, TEXTURE_FILTER_TRILINEAR);  // blends between them

    // Digits sit on the baseline, well below the middle of the text line: measure where their middle really is,
    // so a number can be centered on its string exactly
    int zero = GetGlyphIndex(font, '0');
    tab.digitCenter = (font.glyphs[zero].offsetY + font.recs[zero].height / 2.0f) / FONT_LOAD_SIZE;
    tab.font = font;
    tab.loaded = true;
    return true;
}

void unloadTabFont(){
    if (tab.loaded) UnloadFont(tab.font);
    tab.loaded = false;
}

// Draws text with its middle at (x, y)
static void drawCentered(const char* text, float x, float y, float size, Color color){
    Font font = tab.loaded ? tab.font : GetFontDefault();
    float center = tab.loaded ? tab.digitCenter : 0.5f;
    Vector2 measured = MeasureTextEx(font, text, size, 0.0f);
    DrawTextEx(font, text, {x - measured.x / 2, y - center * size}, size, 0.0f, color);
}

void drawTab(Rectangle area, const std::vector<PlayNote>& notes, const std::vector<float>& barTimes, int stringCount,
             const TimeAxis& axis){
    if (stringCount < 1) return;
    const float spacing = std::min(MAX_LINE_SPACING, area.height / (stringCount + 1));
    const float topLineY = area.y + (area.height - spacing * (stringCount - 1)) / 2;
    const float bottomLineY = topLineY + spacing * (stringCount - 1);
    auto lineY = [&](int stringIndex){ return topLineY + (stringCount - 1 - stringIndex) * spacing; }; // string 0, the lowest, at the bottom
    const float numberSize = NUMBER_SIZE * spacing;
    const float right = area.x + area.width;

    BeginScissorMode((int)area.x, (int)area.y, (int)area.width, (int)area.height);
    DrawRectangleRec(area, PAPER);
    auto drawLines = [&](float fromX, float toX){
        for (int s = 0; s < stringCount; s++) DrawLineEx({fromX, lineY(s)}, {toX, lineY(s)}, LINE_THICKNESS, INK);
    };
    drawLines(area.x, right);
    for (float barTime : barTimes){
        float x = axis.xAt(barTime);
        if (x >= area.x && x <= right) DrawLineEx({x, topLineY}, {x, bottomLineY}, LINE_THICKNESS * 1.4f, INK);
    }
    DrawLineEx({axis.hitLineX, topLineY - spacing}, {axis.hitLineX, bottomLineY + spacing}, 2.0f, HIT_LINE);

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
        float width = MeasureTextEx(tab.loaded ? tab.font : GetFontDefault(), fret, numberSize, 0.0f).x;
        DrawRectangleRec({x - width / 2 - 0.15f * spacing, y - 0.45f * spacing, width + 0.3f * spacing, 0.9f * spacing}, PAPER);
        Color color = note.hitFlash > 0.0f ? (note.wasPerfect ? PERFECT_COLOR : NEAR_COLOR) : INK;
        drawCentered(fret, x, y, numberSize, color);
    }

    // The clef on its own strip of paper: T, A and B stacked down the lines
    float clefRight = area.x + CLEF_AREA_WIDTH * spacing;
    DrawRectangleRec({area.x, area.y, clefRight - area.x, area.height}, PAPER);
    drawLines(area.x, clefRight);
    float letterSize = std::min(1.4f * spacing, (bottomLineY - topLineY + spacing) / 3);
    float clefX = area.x + CLEF_AREA_WIDTH * spacing / 2;
    float middle = (topLineY + bottomLineY) / 2;
    const char* letters[] = { "T", "A", "B" };
    for (int i = 0; i < 3; i++){
        float y = middle + (i - 1) * letterSize;
        DrawRectangleRec({clefX - 0.45f * letterSize, y - 0.42f * letterSize, 0.9f * letterSize, 0.84f * letterSize}, PAPER);
        drawCentered(letters[i], clefX, y, letterSize, INK);
    }
    EndScissorMode();
}
