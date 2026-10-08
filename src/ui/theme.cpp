#include "ui/theme.h"

#include <algorithm>

const float UI_FONT_SIZE = 26.0f;

// The palettes. Dark (the default) is night-time and electric: near-black with a blue cast, cool greys, and one neon
// accent, cyan, for what matters right now; judgements in neon green and hot red. Light is the same in daylight.
struct Palette {
    Color background, card, ink, dim, staffLine, accent, good, bad;
};
const Palette LIGHT = {
    {240, 242, 246, 255}, {255, 255, 255, 255}, {14, 18, 28, 255}, {112, 122, 140, 255},
    {222, 227, 236, 255}, {0, 150, 190, 255}, {0, 160, 100, 255}, {220, 40, 80, 255},
};
const Palette DARK = {
    {7, 9, 15, 255}, {13, 17, 27, 255}, {226, 234, 246, 255}, {100, 114, 138, 255},
    {28, 36, 52, 255}, {0, 229, 255, 255}, {57, 255, 160, 255}, {255, 61, 105, 255},
};

// Good and bad for colour-blind eyes: blue and orange, apart for every kind of colour vision
const Color CB_GOOD_DARK = {96, 170, 255, 255}, CB_BAD_DARK = {255, 160, 40, 255};
const Color CB_GOOD_LIGHT = {0, 100, 220, 255}, CB_BAD_LIGHT = {215, 105, 0, 255};

static struct {
    bool colorBlind = false, reduceMotion = false;
    float uiScale = 1.0f;
} access;

void setAccessibility(bool colorBlind, bool reduceMotion, float uiScale){
    access.colorBlind = colorBlind;
    access.reduceMotion = reduceMotion;
    access.uiScale = std::clamp(uiScale, 0.8f, 1.5f);
}
bool reducedMotion(){ return access.reduceMotion; }
float uiScaleSetting(){ return access.uiScale; }

static struct {
    ThemeMode mode = ThemeMode::Light;
    UiFonts fonts;
    Texture2D arabicWordmark{};
} theme;

static const Palette& palette(){
    return theme.mode == ThemeMode::Dark ? DARK : LIGHT;
}

Color themeColor(UiColor role){
    const Palette& p = palette();
    switch (role){
        case UiColor::Background: return p.background;
        case UiColor::Card:       return p.card;
        case UiColor::Ink:        return p.ink;
        case UiColor::Dim:        return p.dim;
        case UiColor::StaffLine:  return p.staffLine;
        case UiColor::Accent:     return p.accent;
        case UiColor::Good:       return access.colorBlind ? (theme.mode == ThemeMode::Dark ? CB_GOOD_DARK : CB_GOOD_LIGHT) : p.good;
        case UiColor::Bad:        return access.colorBlind ? (theme.mode == ThemeMode::Dark ? CB_BAD_DARK : CB_BAD_LIGHT) : p.bad;
    }
    return p.ink;
}

ImVec4 uiColorVec(UiColor role, float alpha){
    Color c = themeColor(role);
    return ImVec4(c.r / 255.0f, c.g / 255.0f, c.b / 255.0f, c.a / 255.0f * alpha);
}

ImU32 uiColor(UiColor role, float alpha){
    return ImGui::ColorConvertFloat4ToU32(uiColorVec(role, alpha));
}

// A color between two others: for hover and pressed states between the card and the ink
static ImVec4 mix(UiColor from, UiColor to, float amount){
    ImVec4 a = uiColorVec(from), b = uiColorVec(to);
    return ImVec4(a.x + (b.x - a.x) * amount, a.y + (b.y - a.y) * amount, a.z + (b.z - a.z) * amount, 1.0f);
}

static void styleImGui(){
    ImGuiStyle& style = ImGui::GetStyle();
    if (theme.mode == ThemeMode::Dark) ImGui::StyleColorsDark(&style);
    else ImGui::StyleColorsLight(&style);
    style.FontSizeBase = UI_FONT_SIZE;
    style.FrameRounding = 6.0f;
    style.ChildRounding = 8.0f;
    style.PopupRounding = 8.0f;
    style.GrabRounding = 6.0f;
    style.FrameBorderSize = 1.0f;
    style.ItemSpacing = ImVec2(12, 14);
    style.FramePadding = ImVec2(12, 8);

    ImVec4* c = style.Colors;
    c[ImGuiCol_Text] = uiColorVec(UiColor::Ink);
    c[ImGuiCol_TextDisabled] = uiColorVec(UiColor::Dim);
    c[ImGuiCol_WindowBg] = uiColorVec(UiColor::Background);
    c[ImGuiCol_ChildBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_PopupBg] = uiColorVec(UiColor::Card);
    c[ImGuiCol_Border] = uiColorVec(UiColor::StaffLine);
    c[ImGuiCol_BorderShadow] = ImVec4(0, 0, 0, 0);
    // Fields and buttons are cards on the background; hovering leans toward the ink, pressing lights the accent
    c[ImGuiCol_FrameBg] = uiColorVec(UiColor::Card);
    c[ImGuiCol_FrameBgHovered] = mix(UiColor::Card, UiColor::Ink, 0.06f);
    c[ImGuiCol_FrameBgActive] = mix(UiColor::Card, UiColor::Ink, 0.12f);
    c[ImGuiCol_Button] = uiColorVec(UiColor::Card);
    c[ImGuiCol_ButtonHovered] = mix(UiColor::Card, UiColor::Ink, 0.08f);
    c[ImGuiCol_ButtonActive] = uiColorVec(UiColor::Accent, 0.9f);
    c[ImGuiCol_Header] = mix(UiColor::Card, UiColor::Ink, 0.08f);
    c[ImGuiCol_HeaderHovered] = mix(UiColor::Card, UiColor::Ink, 0.12f);
    c[ImGuiCol_HeaderActive] = uiColorVec(UiColor::Accent, 0.9f);
    c[ImGuiCol_Tab] = uiColorVec(UiColor::Background);
    c[ImGuiCol_TabHovered] = mix(UiColor::Card, UiColor::Ink, 0.08f);
    c[ImGuiCol_TabSelected] = uiColorVec(UiColor::Card);
    c[ImGuiCol_TabSelectedOverline] = uiColorVec(UiColor::Accent);
    c[ImGuiCol_CheckMark] = uiColorVec(UiColor::Accent);
    c[ImGuiCol_SliderGrab] = uiColorVec(UiColor::Accent);
    c[ImGuiCol_SliderGrabActive] = uiColorVec(UiColor::Accent);
    c[ImGuiCol_Separator] = uiColorVec(UiColor::StaffLine);
    c[ImGuiCol_SeparatorHovered] = uiColorVec(UiColor::Accent, 0.6f);
    c[ImGuiCol_SeparatorActive] = uiColorVec(UiColor::Accent);
    c[ImGuiCol_ScrollbarBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_ScrollbarGrab] = uiColorVec(UiColor::StaffLine);
    c[ImGuiCol_ScrollbarGrabHovered] = uiColorVec(UiColor::Dim, 0.6f);
    c[ImGuiCol_ScrollbarGrabActive] = uiColorVec(UiColor::Dim);
    c[ImGuiCol_NavCursor] = uiColorVec(UiColor::Accent);
    c[ImGuiCol_TextSelectedBg] = uiColorVec(UiColor::Accent, 0.3f);
    c[ImGuiCol_ModalWindowDimBg] = uiColorVec(UiColor::Ink, 0.25f);
}

void initTheme(const std::string& resourcesDir, ThemeMode mode){
    ImGuiIO& io = ImGui::GetIO();
    auto load = [&](const char* file) -> ImFont* {
        std::string path = resourcesDir + "fonts/" + file;
        if (!FileExists(path.c_str())){
            TraceLog(LOG_WARNING, "Font not found, using ImGui's own: %s", path.c_str());
            return nullptr;
        }
        return io.Fonts->AddFontFromFileTTF(path.c_str(), UI_FONT_SIZE);
    };
    theme.fonts.text = load("Figtree-Medium.ttf");
    theme.fonts.bold = load("Figtree-Bold.ttf");
    theme.fonts.heavy = load("Figtree-ExtraBold.ttf");
    theme.fonts.mono = load("ChivoMono-Regular.ttf");
    if (theme.fonts.text) io.FontDefault = theme.fonts.text;

    std::string wordmark = resourcesDir + "images/lahn-arabic.png";
    if (FileExists(wordmark.c_str())){
        theme.arabicWordmark = LoadTexture(wordmark.c_str());
        GenTextureMipmaps(&theme.arabicWordmark);                  // drawn much smaller than it's stored
        SetTextureFilter(theme.arabicWordmark, TEXTURE_FILTER_TRILINEAR);
        // The image is cropped tight to the letters: a texture that repeats would blend its opposite edge into
        // them when drawn small (faint lines along the edges), so it stops at its edges instead
        SetTextureWrap(theme.arabicWordmark, TEXTURE_WRAP_CLAMP);
    }
    setTheme(mode);
}

void closeTheme(){
    if (theme.arabicWordmark.id != 0) UnloadTexture(theme.arabicWordmark);
    theme.arabicWordmark = {};
}

void setTheme(ThemeMode mode){
    theme.mode = mode;
    styleImGui();
}

ThemeMode currentTheme(){
    return theme.mode;
}

const UiFonts& uiFonts(){
    return theme.fonts;
}

float drawWordmark(ImDrawList* draw, ImVec2 topLeft, float height){
    ImFont* font = theme.fonts.heavy ? theme.fonts.heavy : ImGui::GetFont();
    ImU32 ink = uiColor(UiColor::Ink);
    // Tight letters, like the mockup: each one drawn with a little of the space taken out
    const char* latin = "lahn";
    float x = topLeft.x;
    for (const char* c = latin; *c; c++){
        char letter[2] = { *c, 0 };
        draw->AddText(font, height, ImVec2(x, topLeft.y), ink, letter);
        x += font->CalcTextSizeA(height, FLT_MAX, 0.0f, letter).x - height * 0.045f;
    }
    // The string between the scripts, in the accent
    float gap = height * 0.32f;
    float top = topLeft.y + height * 0.18f, bottom = topLeft.y + height * 0.92f;
    x += gap;
    verticalLine(draw, x, top, bottom, std::max(1.5f, height * 0.035f), uiColor(UiColor::Accent));
    x += gap;
    // The Arabic name: a white image tinted to the ink, as tall as the Latin letters' ascenders
    if (theme.arabicWordmark.id != 0){
        float h = bottom - top + height * 0.08f;
        float w = h * theme.arabicWordmark.width / theme.arabicWordmark.height;
        draw->AddImage(ImTextureID(theme.arabicWordmark.id), ImVec2(x, top - height * 0.04f), ImVec2(x + w, top - height * 0.04f + h), ImVec2(0, 0), ImVec2(1, 1), ink);
        x += w;
    }
    return x - topLeft.x;
}

void horizontalLine(ImDrawList* draw, float x0, float x1, float y, float thickness, ImU32 color){
    draw->AddRectFilled(ImVec2(x0, y - thickness / 2), ImVec2(x1, y + thickness / 2), color);
}

void verticalLine(ImDrawList* draw, float x, float y0, float y1, float thickness, ImU32 color){
    draw->AddRectFilled(ImVec2(x - thickness / 2, y0), ImVec2(x + thickness / 2, y1), color);
}
