#pragma once

#include "core/profile.h"
#include "imgui.h"

// What the player earns, shown as it's earned (app/playerprogress): the XP of a run in a little pill at the top, the
// level's bar filling under it; then, one at a time, a card for the daily goal met (and the streak), a new level,
// each achievement, sliding in from the top with a chime. Drawn over everything, once a frame.
void drawRewards(float scale);

// An achievement's medal: a disc in its tier's metal (bronze, silver, gold), ringed, with a star; dim while locked
void drawMedal(ImDrawList* draw, ImVec2 center, float radius, Tier tier, bool unlocked, float alpha = 1.0f);
ImU32 tierColor(Tier tier, float alpha = 1.0f);
// A five-pointed star, filled
void drawStar(ImDrawList* draw, ImVec2 centre, float radius, ImU32 color);
