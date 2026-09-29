#include "views/viewfont.h"

const int FONT_LOAD_SIZE = 64; // drawn smaller than this, through mipmaps, so it stays smooth

static struct {
    Font font;
    bool loaded = false;
    float digitCenter = 0.5f; // where a digit's middle sits in its text line, as a fraction of the font size
} view;

bool loadViewFont(const std::string& path){
    // Numbers, the tab clef, note names like "F#2 [3]", and the computer keys that play piano ("Z", ",")
    const char* characters = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZb#[] ,./;'-=\\`";
    int count = 0;
    int* codepoints = LoadCodepoints(characters, &count);
    Font font = LoadFontEx(path.c_str(), FONT_LOAD_SIZE, codepoints, count);
    UnloadCodepoints(codepoints);
    if (font.texture.id == 0 || font.texture.id == GetFontDefault().texture.id) return false; // raylib fell back to its own font
    GenTextureMipmaps(&font.texture);                          // smaller copies of the glyphs, for drawing small
    SetTextureFilter(font.texture, TEXTURE_FILTER_TRILINEAR);  // blends between them

    // Digits sit on the baseline, well below the middle of the text line: measure where their middle really is,
    // so a number can be centered on a line exactly
    int zero = GetGlyphIndex(font, '0');
    view.digitCenter = (font.glyphs[zero].offsetY + font.recs[zero].height / 2.0f) / FONT_LOAD_SIZE;
    view.font = font;
    view.loaded = true;
    return true;
}

void unloadViewFont(){
    if (view.loaded) UnloadFont(view.font);
    view.loaded = false;
}

float viewTextWidth(const char* text, float size){
    return MeasureTextEx(view.loaded ? view.font : GetFontDefault(), text, size, 0.0f).x;
}

void drawViewText(const char* text, float x, float y, float size, Color color, float anchor){
    Font font = view.loaded ? view.font : GetFontDefault();
    float center = view.loaded ? view.digitCenter : 0.5f;
    DrawTextEx(font, text, {x - viewTextWidth(text, size) * anchor, y - center * size}, size, 0.0f, color);
}
