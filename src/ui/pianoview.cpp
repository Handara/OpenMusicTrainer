#include "ui/pianoview.h"

#include "ui/theme.h"

#include <algorithm>

const bool IS_BLACK_KEY[12] = { false, true, false, true, false, false, true, false, true, false, true, false };
const int WHITE_BEFORE[12] = { 0, 0, 1, 1, 2, 3, 3, 4, 4, 5, 5, 6 };
const ImU32 WHITE_KEY = IM_COL32(255, 255, 255, 255);
const ImU32 BLACK_KEY = IM_COL32(30, 30, 34, 255);

bool pianoKeyIsBlack(int key){ return IS_BLACK_KEY[key % 12]; }

ImU32 pianoKeyColor(int key){ return pianoKeyIsBlack(key) ? BLACK_KEY : WHITE_KEY; }

ImVec4 pianoKeyRect(ImVec2 origin, float whiteWidth, float height, int key){
    float whiteX = origin.x + ((key / 12) * 7 + WHITE_BEFORE[key % 12]) * whiteWidth;
    if (!pianoKeyIsBlack(key)) return ImVec4(whiteX, origin.y, whiteWidth, height);
    return ImVec4(whiteX + whiteWidth * 0.7f, origin.y, whiteWidth * 0.6f, height * 0.6f);
}

int pianoWhiteKeys(int count){
    int whites = 0;
    for (int key = 0; key < count; key++) if (!pianoKeyIsBlack(key)) whites++;
    return whites;
}

int drawPianoKeys(ImVec2 origin, float whiteWidth, float height, int count,
                  const std::function<PianoKeyStyle(int key, bool hovered)>& style){
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const UiFonts& fonts = uiFonts();
    auto rectFor = [&](int key){ return pianoKeyRect(origin, whiteWidth, height, key); };
    // Which key the mouse is on: black keys first, they sit over the white ones
    int hovered = -1;
    ImVec2 mouse = ImGui::GetMousePos();
    for (int pass = 0; pass < 2 && hovered < 0; pass++){
        for (int key = 0; key < count; key++){
            if (pianoKeyIsBlack(key) != (pass == 0)) continue;
            ImVec4 r = rectFor(key);
            if (mouse.x >= r.x && mouse.x < r.x + r.z && mouse.y >= r.y && mouse.y < r.y + r.w){ hovered = key; break; }
        }
    }
    // White keys, then the black ones over them
    const float labelSize = std::min(15.0f, whiteWidth * 0.45f);
    for (int pass = 0; pass < 2; pass++){
        for (int key = 0; key < count; key++){
            bool black = pianoKeyIsBlack(key);
            if (black != (pass == 1)) continue;
            ImVec4 r = rectFor(key);
            PianoKeyStyle look = style(key, key == hovered);
            ImU32 fill = look.fill ? look.fill : pianoKeyColor(key);
            draw->AddRectFilled(ImVec2(r.x + 0.5f, r.y), ImVec2(r.x + r.z - 0.5f, r.y + r.w), fill, 3.0f);
            if (!black) draw->AddRect(ImVec2(r.x + 0.5f, r.y), ImVec2(r.x + r.z - 0.5f, r.y + r.w), uiColor(UiColor::StaffLine), 3.0f);
            if (look.label.empty()) continue;
            ImVec2 size = fonts.bold ? fonts.bold->CalcTextSizeA(labelSize, FLT_MAX, 0.0f, look.label.c_str()) : ImVec2(0, 0);
            ImU32 ink = look.ink ? look.ink : black || look.fill ? WHITE_KEY : uiColor(UiColor::Ink);
            draw->AddText(fonts.bold, labelSize, ImVec2(r.x + (r.z - size.x) / 2, r.y + r.w - size.y - 6.0f), ink, look.label.c_str());
        }
    }
    return hovered;
}
