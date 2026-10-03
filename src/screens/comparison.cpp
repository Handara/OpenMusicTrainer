#include "screens/comparison.h"

#include "core/music.h"
#include "raylib.h"
#include "ui/theme.h"

#include <algorithm>
#include <cmath>

const float SECOND_WIDTH = 90.0f;   // at a 720-pixel-tall window
const float GUTTER = 44.0f;         // the pitches' names, left of the roll
const float RULER = 22.0f;          // the time along the top
const float MIN_LENGTH_S = 0.12f;   // a written note shorter than this is drawn this long
const float MATCH_S = 0.15f;        // a note played this close to a written one of its pitch was that note

static struct {
    float scroll = 0.0f;            // the song's second at the roll's left edge
    bool placed = false;            // scrolled to the first notes yet
} comparison;

void resetNoteComparison(){
    comparison = {};
}

static ImU32 colorOf(UiColor role, float alpha = 1.0f){ return uiColor(role, alpha); }

void drawNoteComparison(const GameResult& result, ImVec2 min, ImVec2 max, float s){
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const UiFonts& fonts = uiFonts();
    const float rollLeft = min.x + GUTTER * s, rollTop = min.y + RULER * s, width = max.x - rollLeft, height = max.y - rollTop;
    const float perSecond = SECOND_WIDTH * s, shownSeconds = width / perSecond;

    // What there is to show: the song's span, and its pitches
    float first = 1e9f, last = 0.0f;
    int low = 127, high = 0;
    for (const WrittenNote& note : result.written){
        first = std::min(first, note.time);
        last = std::max(last, note.time + std::max(note.length, MIN_LENGTH_S));
        low = std::min(low, note.pitch);
        high = std::max(high, note.pitch);
    }
    for (const HeardPitch& heard : result.heard){
        first = std::min(first, heard.time);
        last = std::max(last, heard.time);
        low = std::min(low, heard.pitch);
        high = std::max(high, heard.pitch);
    }
    if (first > last){ first = 0.0f; last = 1.0f; low = 40; high = 64; }
    low -= 1;
    high += 1;
    const int rows = high - low + 1;
    const float rowHeight = std::max(3.0f * s, height / rows);
    auto rowY = [&](int pitch){ return max.y - (pitch - low + 0.5f) * rowHeight; };

    // Scrolling: the wheel, the arrows, a drag
    const float earliest = first - 1.0f, latest = std::max(earliest, last + 1.0f - shownSeconds);
    if (!comparison.placed){ comparison.scroll = earliest; comparison.placed = true; }
    ImGui::SetCursorScreenPos(ImVec2(rollLeft, min.y));
    ImGui::InvisibleButton("comparison", ImVec2(width, max.y - min.y));
    ImGuiIO& io = ImGui::GetIO();
    if (ImGui::IsItemHovered() && io.MouseWheel != 0.0f) comparison.scroll -= io.MouseWheel * 1.5f;
    if (ImGui::IsItemHovered() && io.MouseWheelH != 0.0f) comparison.scroll += io.MouseWheelH * 1.5f;
    if (ImGui::IsItemActive()) comparison.scroll -= io.MouseDelta.x / perSecond;
    if (ImGui::IsKeyDown(ImGuiKey_LeftArrow)) comparison.scroll -= io.DeltaTime * 6.0f;
    if (ImGui::IsKeyDown(ImGuiKey_RightArrow)) comparison.scroll += io.DeltaTime * 6.0f;
    comparison.scroll = std::clamp(comparison.scroll, earliest, latest);
    if (ImGui::IsItemHovered()) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
    auto timeX = [&](float time){ return rollLeft + (time - comparison.scroll) * perSecond; };

    // The roll: a row per pitch, the sharps and flats a little darker, as on a keyboard; the open strings named
    draw->AddRectFilled(ImVec2(rollLeft, rollTop), max, colorOf(UiColor::Card), 6 * s);
    draw->PushClipRect(ImVec2(rollLeft, rollTop), max, true);
    for (int pitch = low; pitch <= high; pitch++){
        const int pc = pitch % 12;
        const bool black = pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10;
        if (black) draw->AddRectFilled(ImVec2(rollLeft, rowY(pitch) - rowHeight / 2), ImVec2(max.x, rowY(pitch) + rowHeight / 2), colorOf(UiColor::StaffLine, 0.35f));
        if (pc == 0) draw->AddLine(ImVec2(rollLeft, rowY(pitch) + rowHeight / 2), ImVec2(max.x, rowY(pitch) + rowHeight / 2), colorOf(UiColor::StaffLine), 1.0f);
    }
    // A line every second, stronger every five
    for (int second = (int)std::floor(comparison.scroll); second <= comparison.scroll + shownSeconds + 1; second++){
        const float x = timeX((float)second);
        draw->AddLine(ImVec2(x, rollTop), ImVec2(x, max.y), colorOf(UiColor::StaffLine, second % 5 == 0 ? 1.0f : 0.4f), 1.0f);
    }
    // The written notes, lit by how they went
    for (const WrittenNote& note : result.written){
        const float x0 = timeX(note.time), x1 = std::max(x0 + 4 * s, timeX(note.time + std::max(note.length, MIN_LENGTH_S)));
        if (x1 < rollLeft || x0 > max.x) continue;
        const float y = rowY(note.pitch), half = std::max(1.5f * s, rowHeight * 0.4f);
        const UiColor how = !note.hit ? UiColor::Bad : note.perfect ? UiColor::Good : UiColor::Accent;
        if (note.hit) draw->AddRectFilled(ImVec2(x0, y - half), ImVec2(x1, y + half), colorOf(how, 0.35f), 3 * s);
        draw->AddRect(ImVec2(x0, y - half), ImVec2(x1, y + half), colorOf(how, note.hit ? 0.9f : 1.0f), 3 * s, 0, (note.hit ? 1.5f : 2.0f) * s);
    }
    // What was played: right where a written note of its pitch was there, wrong where none was
    for (const HeardPitch& heard : result.heard){
        const float x = timeX(heard.time);
        if (x < rollLeft - 10 * s || x > max.x + 10 * s) continue;
        bool right = false;
        for (const WrittenNote& note : result.written){
            if (note.pitch == heard.pitch && heard.time > note.time - MATCH_S && heard.time < note.time + std::max(note.length, MATCH_S)){ right = true; break; }
        }
        const float radius = std::clamp(rowHeight * 0.42f, 2.5f * s, 6.0f * s);
        draw->AddCircleFilled(ImVec2(x, rowY(heard.pitch)), radius + 1.5f * s, colorOf(UiColor::Card));
        draw->AddCircleFilled(ImVec2(x, rowY(heard.pitch)), radius, colorOf(right ? UiColor::Good : UiColor::Bad));
    }
    draw->PopClipRect();

    // The time along the top, the pitches down the left
    for (int second = (int)std::ceil(comparison.scroll); second <= comparison.scroll + shownSeconds; second++){
        if (second % 5 != 0 || second < 0) continue;
        draw->AddText(fonts.mono, 12 * s, ImVec2(timeX((float)second) + 3 * s, min.y + 3 * s), colorOf(UiColor::Dim), TextFormat("%d:%02d", second / 60, second % 60));
    }
    for (int pitch = low; pitch <= high; pitch++){
        const bool open = std::count(result.tuning.begin(), result.tuning.end(), pitch) > 0;
        const bool named = open || pitch % 12 == 0 || rowHeight >= 14 * s;
        if (!named) continue;
        const float size = std::min(13 * s, rowHeight * 0.95f);
        draw->AddText(fonts.mono, size, ImVec2(min.x, rowY(pitch) - size / 2), colorOf(open ? UiColor::Ink : UiColor::Dim),
                      TextFormat("%s%d", pitchClassName(pitch), pitchOctave(pitch)));
    }
}
