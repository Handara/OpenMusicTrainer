#pragma once

// Going from one screen to another: the old screen's last frame is kept as a picture, and fades out over the new
// screen while drifting up a little. Screens don't know about it; the main loop tells it when the screen changed.

// Call at the end of the frame in which the screen changed, before EndDrawing: it keeps that frame, as it was drawn
void startTransition();
// Call every frame after everything else is drawn (before startTransition): the old picture, fading
void drawTransition();
void unloadTransition();
