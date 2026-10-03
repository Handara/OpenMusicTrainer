#include "views/smooth.h"

#include "rlgl.h"

#include <algorithm>
#include <cmath>

const float FEATHER = 1.0f; // pixels an edge fades out over

// Enough segments that a curve stays smooth at its size: about one every 4 pixels of edge, 24 at the least
static int segmentsFor(float radius, float degrees){
    int full = std::clamp((int)(2.0f * PI * radius / 4.0f), 24, 180);
    return std::max(3, (int)std::ceil(full * degrees / 360.0f));
}

// A band between two radii, from one color at the inner edge to another at the outer, as raylib's DrawRing lays out
// its triangles (the same order, so nothing is culled)
static void band(Vector2 c, float inner, float outer, float start, float end, Color innerColor, Color outerColor){
    if (outer <= inner || end <= start) return;
    const int segments = segmentsFor(outer, end - start);
    const float step = (end - start) / segments;
    rlCheckRenderBatchLimit(6 * segments);
    rlBegin(RL_TRIANGLES);
    for (int i = 0; i < segments; i++){
        float a0 = DEG2RAD * (start + step * i), a1 = DEG2RAD * (start + step * (i + 1));
        float c0 = std::cos(a0), s0 = std::sin(a0), c1 = std::cos(a1), s1 = std::sin(a1);
        rlColor4ub(outerColor.r, outerColor.g, outerColor.b, outerColor.a); rlVertex2f(c.x + c0 * outer, c.y + s0 * outer);
        rlColor4ub(innerColor.r, innerColor.g, innerColor.b, innerColor.a); rlVertex2f(c.x + c0 * inner, c.y + s0 * inner);
        rlVertex2f(c.x + c1 * inner, c.y + s1 * inner);

        rlVertex2f(c.x + c1 * inner, c.y + s1 * inner);
        rlColor4ub(outerColor.r, outerColor.g, outerColor.b, outerColor.a); rlVertex2f(c.x + c1 * outer, c.y + s1 * outer);
        rlVertex2f(c.x + c0 * outer, c.y + s0 * outer);
    }
    rlEnd();
}

static Color clear(Color color){ return { color.r, color.g, color.b, 0 }; }

void smoothCircle(Vector2 center, float radius, Color color){
    if (radius <= 0.0f || color.a == 0) return;
    const float solid = std::max(0.0f, radius - FEATHER / 2);
    // The solid middle as a fan, as raylib's DrawCircleSector lays it out
    const int segments = segmentsFor(radius, 360.0f);
    const float step = 360.0f / segments;
    rlCheckRenderBatchLimit(3 * segments);
    rlBegin(RL_TRIANGLES);
    rlColor4ub(color.r, color.g, color.b, color.a);
    for (int i = 0; i < segments; i++){
        float a0 = DEG2RAD * step * i, a1 = DEG2RAD * step * (i + 1);
        rlVertex2f(center.x, center.y);
        rlVertex2f(center.x + std::cos(a1) * solid, center.y + std::sin(a1) * solid);
        rlVertex2f(center.x + std::cos(a0) * solid, center.y + std::sin(a0) * solid);
    }
    rlEnd();
    band(center, solid, radius + FEATHER / 2, 0.0f, 360.0f, color, clear(color)); // the edge, fading out
}

void smoothRing(Vector2 center, float innerRadius, float outerRadius, float startAngle, float endAngle, Color color){
    if (outerRadius <= innerRadius || endAngle <= startAngle || color.a == 0) return;
    const float middle = (innerRadius + outerRadius) / 2;
    // A ring thinner than the feathers is drawn as thin as it is, with less color, rather than vanishing
    float solidInner = std::min(middle, innerRadius + FEATHER / 2), solidOuter = std::max(middle, outerRadius - FEATHER / 2);
    if (outerRadius - innerRadius < FEATHER) color.a = (unsigned char)(color.a * (outerRadius - innerRadius) / FEATHER);
    band(center, innerRadius - FEATHER / 2, solidInner, startAngle, endAngle, clear(color), color);
    band(center, solidInner, solidOuter, startAngle, endAngle, color, color);
    band(center, solidOuter, outerRadius + FEATHER / 2, startAngle, endAngle, color, clear(color));
}

void smoothLine(Vector2 from, Vector2 to, float thickness, Color color){
    float dx = to.x - from.x, dy = to.y - from.y, length = std::sqrt(dx * dx + dy * dy);
    if (length <= 0.0f || color.a == 0) return;
    if (thickness < FEATHER){
        color.a = (unsigned char)(color.a * thickness / FEATHER);
        thickness = FEATHER;
    }
    // Across the line: its solid core, and a feather either side fading to nothing
    const float nx = -dy / length, ny = dx / length;
    const float core = (thickness - FEATHER) / 2, edge = (thickness + FEATHER) / 2;
    struct Strip { float from, to; Color a, b; };
    const Strip strips[] = { { -edge, -core, clear(color), color }, { -core, core, color, color }, { core, edge, color, clear(color) } };
    rlCheckRenderBatchLimit(18);
    rlBegin(RL_TRIANGLES);
    for (const Strip& strip : strips){
        if (strip.to <= strip.from) continue;
        Vector2 a0 = { from.x + nx * strip.from, from.y + ny * strip.from }, a1 = { to.x + nx * strip.from, to.y + ny * strip.from };
        Vector2 b0 = { from.x + nx * strip.to, from.y + ny * strip.to }, b1 = { to.x + nx * strip.to, to.y + ny * strip.to };
        // Wound like raylib's own quads: counter-clockwise on screen (y down)
        rlColor4ub(strip.a.r, strip.a.g, strip.a.b, strip.a.a); rlVertex2f(a0.x, a0.y);
        rlColor4ub(strip.b.r, strip.b.g, strip.b.b, strip.b.a); rlVertex2f(b0.x, b0.y);
        rlColor4ub(strip.a.r, strip.a.g, strip.a.b, strip.a.a); rlVertex2f(a1.x, a1.y);

        rlColor4ub(strip.a.r, strip.a.g, strip.a.b, strip.a.a); rlVertex2f(a1.x, a1.y);
        rlColor4ub(strip.b.r, strip.b.g, strip.b.b, strip.b.a); rlVertex2f(b0.x, b0.y);
        rlVertex2f(b1.x, b1.y);
    }
    rlEnd();
}

// A piece of a disc, from `start` to `end` degrees, its round edge fading out
static void sector(Vector2 center, float radius, float start, float end, Color color){
    const float solid = std::max(0.0f, radius - FEATHER / 2);
    const int segments = segmentsFor(radius, end - start);
    const float step = (end - start) / segments;
    rlCheckRenderBatchLimit(3 * segments);
    rlBegin(RL_TRIANGLES);
    rlColor4ub(color.r, color.g, color.b, color.a);
    for (int i = 0; i < segments; i++){
        float a0 = DEG2RAD * (start + step * i), a1 = DEG2RAD * (start + step * (i + 1));
        rlVertex2f(center.x, center.y);
        rlVertex2f(center.x + std::cos(a1) * solid, center.y + std::sin(a1) * solid);
        rlVertex2f(center.x + std::cos(a0) * solid, center.y + std::sin(a0) * solid);
    }
    rlEnd();
    band(center, solid, radius + FEATHER / 2, start, end, color, clear(color));
}

void smoothRoundedRect(Rectangle rect, float radius, Color color){
    if (rect.width <= 0.0f || rect.height <= 0.0f || color.a == 0) return;
    // Thinner than its edges' fade (a bar just starting to fill): too small for corners, a plain rectangle
    if (std::min(rect.width, rect.height) < 2 * FEATHER){
        DrawRectangleRec(rect, color);
        return;
    }
    const float r = std::clamp(radius, FEATHER, std::min(rect.width, rect.height) / 2);
    const float half = FEATHER / 2, x = rect.x, y = rect.y, w = rect.width, h = rect.height;
    const Color none = clear(color);
    // The solid body, as three bands that don't overlap: the middle column, then the left and right ones between
    // the corners. Its outer edges stop half a pixel in, where each side's fade takes over.
    DrawRectangleRec({ x + r, y + half, w - 2 * r, h - 2 * half }, color);
    DrawRectangleRec({ x + half, y + r, r - half, h - 2 * r }, color);
    DrawRectangleRec({ x + w - r, y + r, r - half, h - 2 * r }, color);
    // The straight sides fading out over a pixel
    DrawRectangleGradientEx({ x + r, y - half, w - 2 * r, FEATHER }, none, color, none, color);                 // top
    DrawRectangleGradientEx({ x + r, y + h - half, w - 2 * r, FEATHER }, color, none, color, none);             // bottom
    DrawRectangleGradientEx({ x - half, y + r, FEATHER, h - 2 * r }, none, none, color, color);                 // left
    DrawRectangleGradientEx({ x + w - half, y + r, FEATHER, h - 2 * r }, color, color, none, none);             // right
    // The corners (0 degrees points right, angles run clockwise on screen)
    sector({ x + w - r, y + r }, r, 270.0f, 360.0f, color);
    sector({ x + w - r, y + h - r }, r, 0.0f, 90.0f, color);
    sector({ x + r, y + h - r }, r, 90.0f, 180.0f, color);
    sector({ x + r, y + r }, r, 180.0f, 270.0f, color);
}
