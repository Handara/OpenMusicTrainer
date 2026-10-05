#include "ui/neckcards.h"

#include "core/music.h"
#include "ui/theme.h"
#include "views/playnote.h"

#include <algorithm>
#include <cfloat>
#include <cmath>

const float PATH_DOT_GAP = 14.0f;  // at a 720-pixel-tall window: between the way's dots
const float BEND_SHARE = 0.32f;    // a bend arrow's length for each semitone, in the strings' spacing

// Play mode's notes (views/neckview), on this neck: a rounded card in its string's color with its fret on it and its
// name under, the newest lit white. `grow`: pixels added all round (a pop, a ring); `alpha` fades it.
ImU32 imColor(Color color, float alpha){
    return IM_COL32(color.r, color.g, color.b, (int)(color.a * std::clamp(alpha, 0.0f, 1.0f)));
}
Color blend(Color from, Color to, float t){
    return { (unsigned char)(from.r + (to.r - from.r) * t), (unsigned char)(from.g + (to.g - from.g) * t),
             (unsigned char)(from.b + (to.b - from.b) * t), 255 };
}
void cardSize(const FretboardLayout& board, float& halfW, float& halfH){
    halfW = std::min(board.spacing * 0.27f * 1.5f, board.fretWidth * 0.38f);
    halfH = halfW / 1.5f;
}
void cardOutline(ImDrawList* draw, ImVec2 at, float halfW, float halfH, float grow, ImU32 color, float width){
    const float w = halfW + grow, h = halfH + grow;
    draw->AddRect(ImVec2(at.x - w, at.y - h), ImVec2(at.x + w, at.y + h), color, std::max(0.0f, 0.4f * halfH + grow), 0, width);
}
void drawNoteCard(ImDrawList* draw, const FretboardLayout& board, int string, int fret, int pitch, float grow, float alpha,
                         bool newest, float s){
    const ImVec2 at(board.fretX(fret), board.stringY(string));
    float halfW, halfH;
    cardSize(board, halfW, halfH);
    halfW += grow;
    halfH += grow;
    const bool night = currentTheme() == ThemeMode::Dark;
    const Color color = stringColor(string), card = themeColor(UiColor::Card), background = themeColor(UiColor::Background);
    const Color whiteHot = { 236, 246, 255, 255 };
    const float corner = 0.4f * halfH;
    // Its glow, fading out from its edge, then a dark rim that keeps notes apart
    for (int k = 1; k <= 3; k++) cardOutline(draw, at, halfW, halfH, 1.6f * k * s, imColor(newest ? whiteHot : color, alpha * (newest ? 0.4f : 0.22f) / k), 2.0f * s);
    draw->AddRectFilled(ImVec2(at.x - halfW - s, at.y - halfH - s), ImVec2(at.x + halfW + s, at.y + halfH + s), imColor(background, alpha), corner + s);
    const Color fill = newest ? (night ? whiteHot : color) : blend(card, color, night ? 0.16f : 0.14f);
    const Color ink = newest ? (night ? background : WHITE) : (night ? blend(color, WHITE, 0.35f) : blend(color, BLACK, 0.2f));
    draw->AddRectFilled(ImVec2(at.x - halfW, at.y - halfH), ImVec2(at.x + halfW, at.y + halfH), imColor(fill, alpha), corner);
    draw->AddRect(ImVec2(at.x - halfW + s, at.y - halfH + s), ImVec2(at.x + halfW - s, at.y + halfH - s), imColor(color, alpha), corner, 0, (newest ? 2.6f : 2.0f) * s);
    // The fret, and the name under it
    const UiFonts& fonts = uiFonts();
    const float size = halfH * 1.45f;
    const char* number = TextFormat("%d", fret);
    ImVec2 numberSize = fonts.bold->CalcTextSizeA(size * 0.8f, FLT_MAX, 0.0f, number);
    draw->AddText(fonts.bold, size * 0.8f, ImVec2(at.x - numberSize.x / 2, at.y - size * 0.2f - numberSize.y / 2), imColor(ink, alpha), number);
    const char* name = pitchClassName(pitch);
    ImVec2 nameSize = fonts.bold->CalcTextSizeA(size * 0.46f, FLT_MAX, 0.0f, name);
    draw->AddText(fonts.bold, size * 0.46f, ImVec2(at.x - nameSize.x / 2, at.y + size * 0.42f - nameSize.y / 2), imColor(ink, alpha * 0.85f), name);
}

// A slide's way, from `a` to `b`: a tunnel in the string's color between their cards, a light running through it
// (`light`, 0 to 1; negative: none), `alpha` fading it
void drawTunnel(ImDrawList* draw, const FretboardLayout& board, ImVec2 a, ImVec2 b, Color color, float light, float alpha, float s){
    float halfW, halfH;
    cardSize(board, halfW, halfH);
    const float width = halfH * 1.1f;
    draw->AddLine(a, b, imColor(color, 0.18f * alpha), width + 6 * s);        // its glow
    draw->AddLine(a, b, imColor(color, 0.45f * alpha), width);                // the tunnel
    draw->AddLine(a, b, imColor(Color{ 236, 246, 255, 255 }, 0.5f * alpha), 2.0f * s); // its core
    if (light < 0.0f || light >= 1.0f) return;
    const ImVec2 at(a.x + (b.x - a.x) * light, a.y + (b.y - a.y) * light);
    draw->AddCircleFilled(at, width * 0.55f, imColor(Color{ 236, 246, 255, 255 }, 0.9f));
}

// A hammer-on or a pull-off, from `a` to `b`: a slur over them, as in tab, its letter at the top
void drawSlur(ImDrawList* draw, const FretboardLayout& board, ImVec2 a, ImVec2 b, const char* letter, Color color, float alpha, float s){
    float halfW, halfH;
    cardSize(board, halfW, halfH);
    const ImVec2 from(a.x, a.y - halfH - 3 * s), to(b.x, b.y - halfH - 3 * s);
    const ImVec2 top((from.x + to.x) / 2, std::min(from.y, to.y) - board.spacing * 0.55f);
    draw->AddBezierQuadratic(from, top, to, imColor(color, 0.85f * alpha), 2.2f * s);
    const UiFonts& fonts = uiFonts();
    const ImVec2 size = fonts.bold->CalcTextSizeA(13 * s, FLT_MAX, 0.0f, letter);
    const ImVec2 peak((from.x + 2 * top.x + to.x) / 4, (from.y + 2 * top.y + to.y) / 4); // the curve's highest point
    draw->AddText(fonts.bold, 13 * s, ImVec2(peak.x - size.x / 2, peak.y - size.y - 2 * s), imColor(color, alpha), letter);
}

// The hand's way from one note to the next: dots between their cards, from `a` to `b`. `light`: 0 to 1, a light going
// along it (negative: none); `alpha` fades the dots.
void drawWay(ImDrawList* draw, const FretboardLayout& board, ImVec2 a, ImVec2 b, float light, float alpha, float s){
    float halfW, halfH;
    cardSize(board, halfW, halfH);
    const float dx = b.x - a.x, dy = b.y - a.y, length = std::sqrt(dx * dx + dy * dy);
    if (length < 1e-3f) return;
    const float ux = dx / length, uy = dy / length;
    // Where the way leaves a card: its edge in that direction
    const float edge = std::min(std::fabs(ux) > 1e-3f ? halfW / std::fabs(ux) : 1e9f, std::fabs(uy) > 1e-3f ? halfH / std::fabs(uy) : 1e9f) + 4 * s;
    float start = edge, end = length - edge;
    if (end <= start) return; // touching
    const ImU32 accent = uiColor(UiColor::Accent, 0.6f * alpha);
    const float gap = PATH_DOT_GAP * s;
    if (end - start < gap) start = end = (start + end) / 2;
    for (float d = start; d <= end + 0.01f; d += gap){
        if (light >= 0.0f && d > start + (end - start) * light) break; // laid down by the light as it passes
        draw->AddCircleFilled(ImVec2(a.x + ux * d, a.y + uy * d), 2.6f * s, accent);
    }
    if (light < 0.0f || light >= 1.0f) return;
    for (int k = 5; k >= 0; k--){
        const float along = start + (end - start) * std::max(0.0f, light - 0.05f * k), fade = 1.0f - k / 6.0f;
        draw->AddCircleFilled(ImVec2(a.x + ux * along, a.y + uy * along), (2.5f + 2.5f * fade) * s, uiColor(UiColor::Accent, 0.9f * fade));
    }
}

void drawBendArrow(ImDrawList* draw, const FretboardLayout& board, int string, int fret, float semitones, float s){
    float halfW, halfH;
    cardSize(board, halfW, halfH);
    const ImVec2 at(board.fretX(fret), board.stringY(string));
    // Its length by the strings' spacing: a whole tone reaches most of the way to the next string up
    const float top = at.y - halfH - 4 * s - std::min(semitones, 3.0f) * board.spacing * BEND_SHARE;
    const ImU32 ink = uiColor(UiColor::Accent);
    draw->AddLine(ImVec2(at.x, at.y - halfH - 4 * s), ImVec2(at.x, top), ink, 2.5f * s);
    draw->AddTriangleFilled(ImVec2(at.x, top - 7 * s), ImVec2(at.x - 6 * s, top + 2 * s), ImVec2(at.x + 6 * s, top + 2 * s), ink);
    const int quarters = (int)std::lround(semitones * 2.0f); // in quarter tones: a semitone is half a tone
    const char* names[] = { "", "1/4", "1/2", "3/4", "full", "1 1/4", "1 1/2", "1 3/4" };
    const char* amount = quarters >= 1 && quarters <= 7 ? names[quarters] : TextFormat("%.1f", semitones / 2.0f);
    draw->AddText(uiFonts().bold, 15 * s, ImVec2(at.x + 9 * s, top - 6 * s), ink, amount);
}
