#pragma once

#include "raylib.h"

// Anti-aliased shapes for the note views. raylib's own have hard edges that step from pixel to pixel, unless the
// window is multisampled, which not every system does. These fade their edges out over a pixel, the way Dear ImGui
// draws the menus, so a circle is round on any graphics card.

void smoothCircle(Vector2 center, float radius, Color color);
// A ring, or an arc of one from `startAngle` to `endAngle` (degrees, 0 pointing right, clockwise)
void smoothRing(Vector2 center, float innerRadius, float outerRadius, float startAngle, float endAngle, Color color);
void smoothLine(Vector2 from, Vector2 to, float thickness, Color color);
