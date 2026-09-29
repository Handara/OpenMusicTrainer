#include "ui/fretboardview.h"

#include "core/music.h"
#include "raylib.h"
#include "ui/theme.h"

#include <algorithm>
#include <cmath>

const float STRING_SPACING = 30.0f;    // at a 720-pixel-tall window
const float OPEN_COLUMN = 44.0f;       // room left of the nut for open strings
const float PAD = 22.0f;               // above the top string and below the bottom one
const int SINGLE_DOTS[] = { 3, 5, 7, 9, 15, 17, 19, 21 }; // the fret markers on a guitar's neck; 12 and 24 get two

FretboardLayout fretboardLayout(float left, float top, float width, float s, int strings, int firstFret, int lastFret){
    FretboardLayout layout;
    layout.left = left;
    layout.top = top;
    layout.width = width;
    layout.scale = s;
    layout.strings = strings;
    layout.firstFret = firstFret;
    layout.lastFret = lastFret;
    layout.spacing = STRING_SPACING * s;
    layout.height = layout.spacing * (strings - 1) + 2 * PAD * s;
    layout.boardLeft = left + (firstFret == 0 ? OPEN_COLUMN * s : 0.0f);
    int fretCount = lastFret - std::max(firstFret, 1) + 1; // the fretted columns shown
    layout.fretWidth = (left + width - layout.boardLeft) / std::max(1, fretCount);
    return layout;
}

float FretboardLayout::stringY(int string) const {
    return top + PAD * scale + (strings - 1 - string) * spacing;
}

float FretboardLayout::fretX(int fret) const {
    if (fret == 0) return left + OPEN_COLUMN * scale / 2;
    return boardLeft + (fret - std::max(firstFret, 1) + 0.5f) * fretWidth;
}

int FretboardLayout::fretAt(float x) const {
    if (x < left || x >= left + width) return -1;
    if (x < boardLeft) return firstFret == 0 ? 0 : -1;
    return std::min(lastFret, std::max(firstFret, 1) + (int)((x - boardLeft) / fretWidth));
}

int FretboardLayout::stringAt(float y) const {
    int string = strings - 1 - (int)std::floor((y - (top + PAD * scale) + spacing / 2) / spacing);
    return string >= 0 && string < strings ? string : -1;
}

void drawFretboard(const FretboardLayout& layout, const std::vector<int>& tuning, int lit){
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const UiFonts& fonts = uiFonts();
    const float s = layout.scale, pad = PAD * s, top = layout.top, height = layout.height;
    const float right = layout.left + layout.width;
    draw->AddRectFilled(ImVec2(layout.left, top), ImVec2(right, top + height), uiColor(UiColor::Card), 10 * s);

    // Markers between the strings, then the frets, the nut, the strings
    float middle = top + height / 2;
    for (int fret = std::max(layout.firstFret, 1); fret <= layout.lastFret; fret++){
        float x = layout.fretX(fret);
        if (std::count(std::begin(SINGLE_DOTS), std::end(SINGLE_DOTS), fret) > 0){
            draw->AddCircleFilled(ImVec2(x, middle), 6 * s, uiColor(UiColor::StaffLine));
        }
        if (fret == 12 || fret == 24){
            draw->AddCircleFilled(ImVec2(x, middle - layout.spacing), 6 * s, uiColor(UiColor::StaffLine));
            draw->AddCircleFilled(ImVec2(x, middle + layout.spacing), 6 * s, uiColor(UiColor::StaffLine));
        }
        float wireX = std::round(layout.boardLeft + (fret - std::max(layout.firstFret, 1) + 1) * layout.fretWidth); // whole pixels: all alike
        if (fret < layout.lastFret) verticalLine(draw, wireX, top + pad * 0.5f, top + height - pad * 0.5f, 1.5f * s, uiColor(UiColor::Dim, 0.5f));
        const char* number = TextFormat("%d", fret);
        float numberWidth = fonts.mono ? fonts.mono->CalcTextSizeA(13 * s, FLT_MAX, 0.0f, number).x : 0.0f;
        draw->AddText(fonts.mono, 13 * s, ImVec2(x - numberWidth / 2, top + height + 6 * s), uiColor(UiColor::Dim), number);
    }
    verticalLine(draw, layout.boardLeft, top + pad * 0.5f, top + height - pad * 0.5f, (layout.firstFret == 0 ? 4.0f : 1.5f) * s, uiColor(UiColor::Ink));
    for (int string = 0; string < layout.strings; string++){
        bool on = string == lit;
        // Lower strings are thicker, as on the instrument
        horizontalLine(draw, layout.left + 6 * s, right - 6 * s, layout.stringY(string), (on ? 3.0f : 1.0f + 0.25f * (layout.strings - 1 - string)) * s,
                       uiColor(on ? UiColor::Accent : UiColor::Ink, on ? 1.0f : 0.55f));
        // The string's name, left of the board
        const char* name = pitchClassName(tuning[string]);
        float nameWidth = fonts.bold ? fonts.bold->CalcTextSizeA(15 * s, FLT_MAX, 0.0f, name).x : 0.0f;
        draw->AddText(fonts.bold, 15 * s, ImVec2(layout.left - nameWidth - 12 * s, layout.stringY(string) - 8 * s),
                      uiColor(on ? UiColor::Accent : UiColor::Dim), name);
    }
}

void drawFretDot(const FretboardLayout& layout, int string, int fret, float radius, ImU32 fill, ImU32 ink, const char* name){
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const UiFonts& fonts = uiFonts();
    const float s = layout.scale;
    ImVec2 at(layout.fretX(fret), layout.stringY(string));
    draw->AddCircleFilled(at, radius, fill);
    if (!name || !*name) return;
    ImVec2 size = fonts.bold ? fonts.bold->CalcTextSizeA(13 * s, FLT_MAX, 0.0f, name) : ImVec2(0, 0);
    draw->AddText(fonts.bold, 13 * s, ImVec2(at.x - size.x / 2, at.y - size.y / 2), ink, name);
}
