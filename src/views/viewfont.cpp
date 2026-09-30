#include "views/viewfont.h"

#include "core/music.h"
#include "views/playnote.h"

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

void drawNoteLabel(int fret, int pitch, NoteLabel label, float x, float y, float size, Color color){
    const char* number = TextFormat("%d", fret);
    switch (label){
        case NoteLabel::Fret: drawViewText(number, x, y, size, color); break;
        case NoteLabel::Name: drawViewText(pitchClassName(pitch), x, y, size * 0.9f, color); break;
        case NoteLabel::Both:
            drawViewText(number, x, y - size * 0.2f, size * 0.8f, color);
            drawViewText(pitchClassName(pitch), x, y + size * 0.42f, size * 0.46f, Fade(color, color.a / 255.0f * 0.85f));
            break;
    }
}

// Each string its own color, lowest first, as on the neck: red, orange, yellow, green, blue, purple
static const Color STRING_COLORS[] = { {200, 70, 62, 255}, {214, 120, 40, 255}, {190, 145, 30, 255},
                                       {52, 140, 90, 255}, {50, 120, 190, 255}, {128, 90, 190, 255} };

Color stringColor(int stringIndex){ return STRING_COLORS[stringIndex % 6]; }
