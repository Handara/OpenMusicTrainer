#include "ui/transition.h"

#include "raylib.h"

#include <algorithm>

const float DURATION = 0.2f;   // seconds: quick enough to never be in the way
const float DRIFT = 14.0f;     // how far the old picture rises as it goes, at a 720-pixel-tall window
const float ZOOM_DURATION = 0.4f;
const float ZOOM = 2.2f;       // how far a zooming picture grows before it's gone

static Texture2D picture = {};
static double started = 0.0;
static float zoomX = -1.0f, zoomY = -1.0f;

void startTransition(float x, float y){
    unloadTransition();
    // The frame being drawn, read back before it's shown. Once per change of screen, so reading it back is cheap.
    Image frame = LoadImageFromScreen();
    picture = LoadTextureFromImage(frame);
    UnloadImage(frame);
    SetTextureFilter(picture, TEXTURE_FILTER_BILINEAR); // it's drawn off the pixel grid while it drifts
    started = GetTime();
    zoomX = x;
    zoomY = y;
}

void drawTransition(){
    if (picture.id == 0) return;
    bool zooming = zoomX >= 0.0f;
    float t = (float)(GetTime() - started) / (zooming ? ZOOM_DURATION : DURATION);
    if (t >= 1.0f){
        unloadTransition();
        return;
    }
    float eased = 1.0f - (1.0f - t) * (1.0f - t); // fast at first, settling at the end
    float width = (float)GetScreenWidth(), height = (float)GetScreenHeight();
    float rise = DRIFT * height / 720.0f * eased;
    // The picture may be larger than the screen in pixels (high-DPI displays): drawn at the screen's size
    Rectangle source = { 0.0f, 0.0f, (float)picture.width, (float)picture.height };
    Rectangle target = { 0.0f, -rise, width, height };
    if (zooming){
        // Growing around the point: it stays where it is on screen while everything else rushes past it
        float scale = 1.0f + (ZOOM - 1.0f) * eased;
        target = { zoomX - zoomX * scale, zoomY - zoomY * scale, width * scale, height * scale };
    }
    DrawTexturePro(picture, source, target, { 0.0f, 0.0f }, 0.0f, Fade(WHITE, 1.0f - eased));
}

void unloadTransition(){
    if (picture.id != 0) UnloadTexture(picture);
    picture = {};
}
