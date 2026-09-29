#pragma once

// Going from one screen to another: the old screen's last frame is kept as a picture, and fades out over the new
// screen while drifting up a little. Screens don't know about it; the main loop tells it when the screen changed.

// Call at the end of the frame in which the screen changed, before EndDrawing: it keeps that frame, as it was drawn.
// Given a point, the old frame zooms into it as it fades instead (the end of a song, into its timing distribution).
void startTransition(float zoomX = -1.0f, float zoomY = -1.0f);
// Call every frame after everything else is drawn (before startTransition): the old picture, fading
void drawTransition();
void unloadTransition();
