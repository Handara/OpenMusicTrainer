#include "ui/transition.h"

#include "raylib.h"

#include <algorithm>

const float DURATION = 0.2f;   // seconds: quick enough to never be in the way
const float DRIFT = 14.0f;     // how far the old picture rises as it goes, at a 720-pixel-tall window

static Texture2D picture = {};
static double started = 0.0;

void startTransition(){
    unloadTransition();
    // The frame being drawn, read back before it's shown. Once per change of screen, so reading it back is cheap.
    Image frame = LoadImageFromScreen();
    picture = LoadTextureFromImage(frame);
    UnloadImage(frame);
    SetTextureFilter(picture, TEXTURE_FILTER_BILINEAR); // it's drawn off the pixel grid while it drifts
    started = GetTime();
}

void drawTransition(){
    if (picture.id == 0) return;
    float t = (float)(GetTime() - started) / DURATION;
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
    DrawTexturePro(picture, source, target, { 0.0f, 0.0f }, 0.0f, Fade(WHITE, 1.0f - eased));
}

void unloadTransition(){
    if (picture.id != 0) UnloadTexture(picture);
    picture = {};
}
