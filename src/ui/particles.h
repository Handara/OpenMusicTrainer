#pragma once

#include "imgui.h"

// Sparks: a burst of little dots flying out from a point and fading, for a note hit, a milestone. Drawn over
// everything, once a frame (drawParticles moves and fades them). A few hundred at most: the oldest give way.
void spawnBurst(ImVec2 at, ImU32 color, int count, float speed, float scale);
void drawParticles(float scale);
