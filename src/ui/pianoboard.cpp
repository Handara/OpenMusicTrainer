#include "ui/pianoboard.h"

#include "core/music.h"
#include "core/pianokeys.h"
#include "ui/theme.h"

#include <algorithm>
#include <string>

ImVec4 PianoBoard::keyRect(int pitch) const {
    if (!shows(pitch)) return ImVec4(0, 0, 0, 0);
    return pianoKeyRect(origin, whiteWidth, height, pitch - firstPitch);
}

PianoBoard pianoBoard(float left, float top, float width, float maxHeight, int lowPitch, int highPitch){
    PianoBoard board;
    // Three octaves at least, like a small keyboard; notes within two of them with an octave below as well, so they
    // sit towards the middle, where they are on a piano
    board.firstPitch = pianoBaseFor(std::max(0, std::min(lowPitch, highPitch)));
    int octaves = (std::max(lowPitch, highPitch) - board.firstPitch) / 12 + 1;
    if (octaves <= 2 && board.firstPitch >= 12){
        board.firstPitch -= 12;
        octaves++;
    }
    octaves = std::max(3, octaves);
    board.keys = octaves * 12 + 1; // ending on a C
    const int whites = pianoWhiteKeys(board.keys);
    board.whiteWidth = width / whites;
    board.height = std::min(maxHeight, board.whiteWidth * 5.0f);
    board.origin = ImVec2(left, top);
    return board;
}

int drawPianoBoard(const PianoBoard& board, float s, const std::function<PianoKeyStyle(int pitch)>& style){
    const int hovered = drawPianoKeys(board.origin, board.whiteWidth, board.height, board.keys, [&](int key, bool){
        const int pitch = board.firstPitch + key;
        PianoKeyStyle look = style(pitch);
        if (look.label.empty() && pitch % 12 == 0) look.label = "C" + std::to_string(pitch / 12 - 1);
        if (!look.ink && !pianoKeyIsBlack(key)) look.ink = IM_COL32(52, 58, 70, 255); // dark on the white keys, lit or not
        return look;
    });
    // Middle C: a dot over its key
    if (board.shows(60)){
        const ImVec4 rect = board.keyRect(60);
        ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(rect.x + rect.z / 2, rect.y + rect.w * 0.62f), 3.5f * s, uiColor(UiColor::Accent), 12);
    }
    if (hovered >= 0 && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) return board.firstPitch + hovered;
    return -1;
}
