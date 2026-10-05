#include "ui/scoreboard.h"

#include "imgui.h"
#include "raylib.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <functional>

const float PULSE_S = 0.45f;     // a changed value's pulse, from swollen to settled
const float PULSE_GROW = 0.12f;  // how much bigger it starts

float drawScoreboard(const std::vector<ScoreTile>& tiles, float right, float top, float s){
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const UiFonts& fonts = uiFonts();
    ImGuiStorage* storage = ImGui::GetStateStorage();
    const float pad = 14 * s, gap = 10 * s, labelSize = 11 * s, valueSize = 30 * s, subSize = 12 * s;
    const float height = pad + labelSize + 6 * s + valueSize + (std::any_of(tiles.begin(), tiles.end(), [](const ScoreTile& t){ return !t.sub.empty(); }) ? 4 * s + subSize : 0.0f) + pad;
    float x = right;
    const float now = (float)GetTime();
    for (auto it = tiles.rbegin(); it != tiles.rend(); ++it){
        const ScoreTile& tile = *it;
        // When its value last changed: kept between frames by its label (a first sight isn't a change)
        const ImGuiID id = ImGui::GetID(("scoreboard/" + tile.label).c_str());
        const int hash = (int)std::hash<std::string>{}(tile.value);
        const ImGuiID hashId = id + 1, timeId = id + 2, seenId = id + 3;
        if (!storage->GetBool(seenId, false)){
            storage->SetBool(seenId, true);
            storage->SetInt(hashId, hash);
            storage->SetFloat(timeId, -100.0f);
        } else if (storage->GetInt(hashId) != hash){
            storage->SetInt(hashId, hash);
            storage->SetFloat(timeId, now);
        }
        const float since = now - storage->GetFloat(timeId, -100.0f);
        const float pulse = since >= 0.0f && since < PULSE_S ? 1.0f - since / PULSE_S : 0.0f;

        const float valueWidth = fonts.heavy->CalcTextSizeA(valueSize, FLT_MAX, 0.0f, tile.value.c_str()).x;
        const float labelWidth = fonts.mono->CalcTextSizeA(labelSize, FLT_MAX, 0.0f, tile.label.c_str()).x;
        const float subWidth = tile.sub.empty() ? 0.0f : fonts.text->CalcTextSizeA(subSize, FLT_MAX, 0.0f, tile.sub.c_str()).x;
        const float width = std::max({ 96 * s, valueWidth + 2 * pad, labelWidth + 2 * pad, subWidth + 2 * pad });
        x -= width;
        // The card, swollen while it pulses, its edge and glow in the tile's color
        const float grow = PULSE_GROW * pulse * pulse * height * 0.5f;
        const ImVec2 min(x - grow, top - grow), max(x + width + grow, top + height + grow);
        if (pulse > 0.0f) for (int k = 1; k <= 3; k++)
            draw->AddRect(ImVec2(min.x - 2 * k * s, min.y - 2 * k * s), ImVec2(max.x + 2 * k * s, max.y + 2 * k * s), uiColor(tile.color, 0.35f * pulse / k), 12 * s + 2 * k * s, 0, 2 * s);
        draw->AddRectFilled(min, max, uiColor(UiColor::Card), 12 * s);
        draw->AddRect(min, max, pulse > 0.0f ? uiColor(tile.color, 0.4f + 0.6f * pulse) : uiColor(UiColor::StaffLine), 12 * s, 0, (1.0f + 1.5f * pulse) * s);
        // Its label, its value big in its color, and what's under it
        float y = top + pad;
        draw->AddText(fonts.mono, labelSize, ImVec2(x + pad, y), uiColor(UiColor::Dim), tile.label.c_str());
        y += labelSize + 6 * s;
        draw->AddText(fonts.heavy, valueSize, ImVec2(x + pad, y), uiColor(tile.color), tile.value.c_str());
        y += valueSize + 4 * s;
        if (!tile.sub.empty()) draw->AddText(fonts.text, subSize, ImVec2(x + pad, y), uiColor(UiColor::Dim), tile.sub.c_str());
        x -= gap;
    }
    return height;
}
