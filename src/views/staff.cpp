#include "views/staff.h"

#include "core/notation.h"

#include <algorithm>

// SMuFL code points: the music font standard puts every symbol at the same code in every font
const int GLYPH_TREBLE_CLEF_8VB = 0xE052; // treble clef with a small 8 below: "sounds an octave lower", i.e. guitar
const int GLYPH_NOTEHEAD_BLACK = 0xE0A4;
const int GLYPH_SHARP = 0xE262;
// Glyphs are rendered once into a texture at this size, then scaled down when drawn (sharper than scaling up).
// Music fonts have a very tall line height, and raylib sizes fonts by it, so the glyphs themselves come out
// small for a given size: loading big keeps them detailed.
const int FONT_LOAD_SIZE = 768;

const float STAFF_SPACES_TALL = 16.0f; // the area's height in staff spaces: the staff, plus room for ledger lines
const float STEM_LENGTH = 3.5f;        // in staff spaces, the engraving standard
const float LINE_THICKNESS = 0.11f;
const float CLEF_AREA_WIDTH = 3.6f;    // notes that have passed the hit line slide under the clef

const Color PAPER = { 242, 232, 212, 255 };
const Color INK = { 34, 26, 22, 255 };
const Color HIT_LINE = { 200, 150, 40, 255 };
const Color PERFECT_COLOR = { 40, 170, 80, 255 };
const Color NEAR_COLOR = { 210, 150, 20, 255 };

static struct {
    Font font;
    bool loaded = false;
    // Measured once from the loaded font (in pixels at FONT_LOAD_SIZE): where the baseline sits inside a
    // glyph's cell, and the notehead's size. SMuFL makes a black notehead exactly one staff space tall,
    // so its height tells us how big to draw everything, whatever the font's own metrics are.
    float baselineFromTop = 0.0f;
    float noteheadOffsetX = 0.0f;
    float noteheadWidth = 0.0f;
    float noteheadHeight = 1.0f;
} music;

bool loadStaffFont(const std::string& path){
    int codepoints[] = { GLYPH_TREBLE_CLEF_8VB, GLYPH_NOTEHEAD_BLACK, GLYPH_SHARP };
    Font font = LoadFontEx(path.c_str(), FONT_LOAD_SIZE, codepoints, 3);
    if (font.texture.id == 0 || font.texture.id == GetFontDefault().texture.id) return false; // raylib falls back to its default font
    SetTextureFilter(font.texture, TEXTURE_FILTER_BILINEAR); // smooth when drawn smaller than it was loaded

    // A SMuFL notehead is centered on its baseline, so the middle of its image is where the baseline is
    int notehead = GetGlyphIndex(font, GLYPH_NOTEHEAD_BLACK);
    music.baselineFromTop = font.glyphs[notehead].offsetY + font.recs[notehead].height / 2.0f;
    music.noteheadOffsetX = (float)font.glyphs[notehead].offsetX;
    music.noteheadWidth = font.recs[notehead].width;
    music.noteheadHeight = font.recs[notehead].height;
    music.font = font;
    music.loaded = true;
    return true;
}

void unloadStaffFont(){
    if (music.loaded) UnloadFont(music.font);
    music.loaded = false;
}

// How much to shrink the loaded glyphs so a notehead is one staff space tall
static float glyphScale(float staffSpace){
    return staffSpace / music.noteheadHeight;
}

// Draws a glyph with its baseline at y; x is the left edge of the glyph's cell
static void drawGlyph(int codepoint, float x, float y, float staffSpace, Color color){
    float scale = glyphScale(staffSpace);
    DrawTextCodepoint(music.font, codepoint, {x, y - music.baselineFromTop * scale}, FONT_LOAD_SIZE * scale, color);
}

void drawStaff(Rectangle area, const std::vector<PlayNote>& notes, const std::vector<float>& barTimes, const TimeAxis& axis){
    const float space = area.height / STAFF_SPACES_TALL;           // one staff space, in pixels
    const float bottomLineY = area.y + area.height - 5.0f * space; // leaves room for the low E's three ledger lines
    auto yAt = [&](int position){ return bottomLineY - position * space * 0.5f; };
    const float thickness = std::max(1.0f, LINE_THICKNESS * space);
    const float right = area.x + area.width;

    BeginScissorMode((int)area.x, (int)area.y, (int)area.width, (int)area.height); // nothing drawn outside the area
    DrawRectangleRec(area, PAPER);
    auto drawStaffLines = [&](float fromX, float toX){
        for (int line = 0; line <= STAFF_TOP_LINE; line += 2) DrawLineEx({fromX, yAt(line)}, {toX, yAt(line)}, thickness, INK);
    };
    drawStaffLines(area.x, right);

    for (float barTime : barTimes){
        float x = axis.xAt(barTime);
        if (x >= area.x && x <= right) DrawLineEx({x, yAt(STAFF_TOP_LINE)}, {x, yAt(0)}, thickness * 1.4f, INK);
    }
    DrawLineEx({axis.hitLineX, area.y + space}, {axis.hitLineX, area.y + area.height - space}, 2.0f, HIT_LINE);

    const float noteheadWidth = music.loaded ? music.noteheadWidth * glyphScale(space) : 1.18f * space;
    float firstVisibleTime = axis.timeAt(area.x - 3 * space);
    auto it = std::lower_bound(notes.begin(), notes.end(), firstVisibleTime,
                               [](const PlayNote& note, float time){ return note.time < time; });
    for (; it != notes.end(); ++it){
        const PlayNote& note = *it;
        float x = axis.xAt(note.time);
        if (x > right + 3 * space) break;

        StaffNote staffNote = trebleStaffNote(note.pitch + GUITAR_WRITTEN_OCTAVE_SHIFT);
        float y = yAt(staffNote.position);
        Color color = note.hitFlash > 0.0f ? (note.wasPerfect ? PERFECT_COLOR : NEAR_COLOR) : INK;
        float left = x - noteheadWidth / 2;

        // Ledger lines, a little wider than the notehead, on every line position between the staff and the note
        for (int p = -2; p >= staffNote.position; p -= 2) DrawLineEx({left - 0.3f * space, yAt(p)}, {left + noteheadWidth + 0.3f * space, yAt(p)}, thickness, INK);
        for (int p = STAFF_TOP_LINE + 2; p <= staffNote.position; p += 2) DrawLineEx({left - 0.3f * space, yAt(p)}, {left + noteheadWidth + 0.3f * space, yAt(p)}, thickness, INK);

        // Stem: on the notehead's right going up, or its left going down
        bool up = stemUp(staffNote.position);
        float stemX = up ? left + noteheadWidth - thickness / 2 : left + thickness / 2;
        DrawLineEx({stemX, y}, {stemX, y + (up ? -1 : 1) * STEM_LENGTH * space}, thickness * 1.2f, color);

        if (music.loaded){
            drawGlyph(GLYPH_NOTEHEAD_BLACK, left - music.noteheadOffsetX * glyphScale(space), y, space, color);
            if (staffNote.accidental > 0) drawGlyph(GLYPH_SHARP, left - 1.3f * space, y, space, color);
        } else {
            DrawEllipse((int)x, (int)y, noteheadWidth / 2, space * 0.45f, color);
            if (staffNote.accidental > 0) DrawText("#", (int)(left - 1.2f * space), (int)(y - space), (int)(2 * space), color);
        }
    }

    // The clef sits on its own strip of paper, so notes that have been played slide underneath it
    float clefRight = area.x + CLEF_AREA_WIDTH * space;
    DrawRectangleRec({area.x, area.y, clefRight - area.x, area.height}, PAPER);
    drawStaffLines(area.x, clefRight);
    if (music.loaded) drawGlyph(GLYPH_TREBLE_CLEF_8VB, area.x + 0.5f * space, yAt(2), space, INK); // a G clef curls around the G line
    else DrawText("G", (int)(area.x + 0.8f * space), (int)yAt(6), (int)(3 * space), INK);
    EndScissorMode();
}
