#include "screens/practicescreen.h"

#include "core/chart.h"
#include "imgui.h"
#include "raylib.h"
#include "ui/menulist.h"
#include "ui/settingsui.h"
#include "ui/theme.h"
#include "ui/ui.h"
#include "views/playnote.h"

#include <algorithm>
#include <cmath>

const int AROUND_BARS = 4;          // from the pause menu: this many bars, from the one it was paused in
const float EDGE_GRIP = 8.0f;       // pixels either side of the section's edge that take hold of it
const int TEMPOS[] = { 40, 50, 60, 70, 80, 90, 100 };
const int REPEATS[] = { 0, 3, 5, 10, -1 }; // 0: until mastered; -1: a number of the player's own

static struct PracticeState {
    SongEntry song;
    int part = 0;
    Chart chart;
    std::vector<FrettedNote> notes; // the part's, a keys part's pitches as frets on one string
    int strings = 1;
    int bars = 1;
    int fromBar = 0, toBar = 1;     // the section: bars fromBar up to (not including) toBar
    std::string message;
    // How: kept from one practice to the next
    int tempo = 4;                  // into TEMPOS
    bool gradual = true;
    int step = 0;                   // +5% or +10%
    bool noteByNote = false;
    int repeat = 0;                 // into REPEATS
    int ownRepeats = 20;            // the number of the player's own (1 to 100)
    // The mouse on the timeline
    enum class Drag { None, Select, From, To } drag = Drag::None;
    int anchorBar = 0;
} practice;

bool openPracticeScreen(const SongEntry& song, int part, double aroundSeconds, const std::string& message, std::string& error){
    Chart chart;
    if (!loadChart(song.chartPath, chart, error)) return false;
    if (part < 0 || part >= partCount(chart)){
        error = "the song has no part " + std::to_string(part + 1);
        return false;
    }
    const bool sameSong = practice.song.chartPath == song.chartPath && practice.part == part;
    practice.song = song;
    practice.part = part;
    practice.chart = chart;
    practice.notes.clear();
    if (isKeysPart(chart, part)){
        for (const KeysNote& note : chart.keysTracks[part - chart.frettedTracks.size()].notes) practice.notes.push_back({ note.tick, 0, note.pitch, note.duration });
        practice.strings = 1;
    } else {
        practice.notes = chart.frettedTracks[part].notes;
        practice.strings = (int)chart.frettedTracks[part].tuning.size();
    }
    practice.bars = std::max(1, barNumberAt(chart, std::max(0, chart.endTick - 1)) + 1);
    practice.message = message;
    practice.drag = PracticeState::Drag::None;
    if (aroundSeconds >= 0.0){
        int bar = std::clamp(barNumberAt(chart, std::max(0, (int)secondsToTick(chart, aroundSeconds))), 0, practice.bars - 1);
        practice.fromBar = std::min(bar, std::max(0, practice.bars - AROUND_BARS));
        practice.toBar = std::min(practice.bars, practice.fromBar + AROUND_BARS);
    } else if (!sameSong){
        // The first bars with notes in them
        int first = practice.notes.empty() ? 0 : barNumberAt(chart, practice.notes.front().tick);
        practice.fromBar = std::clamp(first, 0, practice.bars - 1);
        practice.toBar = std::min(practice.bars, practice.fromBar + AROUND_BARS);
    }
    practice.fromBar = std::clamp(practice.fromBar, 0, practice.bars - 1);
    practice.toBar = std::clamp(practice.toBar, practice.fromBar + 1, practice.bars);
    return true;
}

PracticeOptions practiceChoice(){
    PracticeOptions options;
    options.on = true;
    options.fromTick = barStartTick(practice.chart, practice.fromBar);
    options.toTick = barStartTick(practice.chart, practice.toBar);
    options.speed = TEMPOS[practice.tempo] / 100.0f;
    options.gradual = practice.gradual && practice.tempo < (int)(sizeof TEMPOS / sizeof TEMPOS[0]) - 1;
    options.step = practice.step == 0 ? 0.05f : 0.10f;
    options.noteByNote = practice.noteByNote;
    options.passes = REPEATS[practice.repeat] < 0 ? practice.ownRepeats : REPEATS[practice.repeat];
    return options;
}

static std::string clock(double seconds){
    seconds = std::max(0.0, seconds);
    return TextFormat("%d:%04.1f", (int)seconds / 60, std::fmod(seconds, 60.0));
}

// The timeline: the song's bars side by side, the part's notes on them (a row per string), the section lit
static void drawTimeline(ImVec2 min, ImVec2 max, float s){
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const UiFonts& fonts = uiFonts();
    const Chart& chart = practice.chart;
    const float width = max.x - min.x, numbers = 20 * s, rowsTop = min.y + numbers, rowsBottom = max.y;
    const int endTick = barStartTick(chart, practice.bars);
    auto tickX = [&](double tick){ return min.x + (float)(tick / std::max(1, endTick)) * width; };
    auto barX = [&](int bar){ return tickX(barStartTick(chart, bar)); };
    auto barAt = [&](float x){
        int bar = 0;
        while (bar + 1 < practice.bars && barX(bar + 1) <= x) bar++;
        return bar;
    };

    draw->AddRectFilled(ImVec2(min.x, rowsTop), ImVec2(max.x, rowsBottom), uiColor(UiColor::Card), 6 * s);
    // Bar lines, and every few bars a number (as many as fit)
    const float barWidth = width / practice.bars;
    const int every = barWidth >= 28 * s ? 1 : barWidth >= 14 * s ? 2 : barWidth >= 7 * s ? 4 : barWidth >= 3.5f * s ? 8 : 16;
    for (int bar = 0; bar < practice.bars; bar++){
        float x = barX(bar);
        if (bar > 0) verticalLine(draw, x, rowsTop + 3 * s, rowsBottom - 3 * s, 1.0f, uiColor(UiColor::StaffLine, every == 1 || bar % every == 0 ? 1.0f : 0.5f));
        if (bar % every == 0) draw->AddText(fonts.mono, 12 * s, ImVec2(x + 3 * s, min.y), uiColor(UiColor::Dim), TextFormat("%d", bar + 1));
    }
    // The section, under the notes
    const float fromX = barX(practice.fromBar), toX = barX(practice.toBar);
    draw->AddRectFilled(ImVec2(fromX, rowsTop), ImVec2(toX, rowsBottom), uiColor(UiColor::Accent, 0.16f));
    // The notes: a short mark each, on its string's row, in its string's color
    const float rowHeight = (rowsBottom - rowsTop - 10 * s) / std::max(1, practice.strings);
    int lowest = 127, highest = 0;
    for (const FrettedNote& note : practice.notes){ lowest = std::min(lowest, note.fret); highest = std::max(highest, note.fret); }
    for (const FrettedNote& note : practice.notes){
        float x = tickX(note.tick), y;
        if (practice.strings == 1){ // keys: by pitch
            float u = highest > lowest ? (float)(note.fret - lowest) / (highest - lowest) : 0.5f;
            y = rowsBottom - 5 * s - u * (rowsBottom - rowsTop - 10 * s);
        } else {
            int row = practice.strings - 1 - note.stringIndex; // the highest string on top
            y = rowsTop + 5 * s + (row + 0.5f) * rowHeight;
        }
        const Color color = stringColor(practice.strings == 1 ? 0 : note.stringIndex);
        const bool inside = x >= fromX && x < toX;
        draw->AddRectFilled(ImVec2(x, y - 1.5f * s), ImVec2(x + std::max(2.0f * s, barWidth * 0.08f), y + 1.5f * s),
                            IM_COL32(color.r, color.g, color.b, inside ? 255 : 110), 1.5f * s);
    }
    // Its edges, to take hold of
    for (float x : { fromX, toX }){
        verticalLine(draw, x, rowsTop, rowsBottom, 2.5f * s, uiColor(UiColor::Accent));
        draw->AddRectFilled(ImVec2(x - 4 * s, rowsTop + (rowsBottom - rowsTop) / 2 - 12 * s), ImVec2(x + 4 * s, rowsTop + (rowsBottom - rowsTop) / 2 + 12 * s),
                            uiColor(UiColor::Accent), 3 * s);
    }

    // The mouse: a drag across bars chooses them; an edge dragged moves it
    ImGui::SetCursorScreenPos(min);
    ImGui::InvisibleButton("timeline", ImVec2(width, max.y - min.y));
    const float mouseX = ImGui::GetIO().MousePos.x;
    const bool nearFrom = std::fabs(mouseX - fromX) <= EDGE_GRIP * s, nearTo = std::fabs(mouseX - toX) <= EDGE_GRIP * s;
    if (ImGui::IsItemHovered()) ImGui::SetMouseCursor(nearFrom || nearTo ? ImGuiMouseCursor_ResizeEW : ImGuiMouseCursor_Hand);
    if (ImGui::IsItemActivated()){
        if (nearTo) practice.drag = PracticeState::Drag::To;
        else if (nearFrom) practice.drag = PracticeState::Drag::From;
        else {
            practice.drag = PracticeState::Drag::Select;
            practice.anchorBar = barAt(mouseX);
        }
    }
    if (ImGui::IsItemActive()){
        // Edges go to the nearest bar line; a selection covers the bars between the press and the mouse
        int line = std::clamp((int)std::lround((mouseX - min.x) / width * practice.bars), 0, practice.bars);
        for (int bar = 0; bar <= practice.bars; bar++) if (std::fabs(barX(bar) - mouseX) < std::fabs(barX(line) - mouseX)) line = bar;
        switch (practice.drag){
            case PracticeState::Drag::From: practice.fromBar = std::min(line, practice.toBar - 1); break;
            case PracticeState::Drag::To: practice.toBar = std::max(line, practice.fromBar + 1); break;
            case PracticeState::Drag::Select: {
                int bar = barAt(mouseX);
                practice.fromBar = std::min(bar, practice.anchorBar);
                practice.toBar = std::max(bar, practice.anchorBar) + 1;
                break;
            }
            case PracticeState::Drag::None: break;
        }
    } else {
        practice.drag = PracticeState::Drag::None;
    }
}

PracticeChoice practiceScreen(){
    PracticeChoice choice = PracticeChoice::None;
    beginMenu("Practice");
    const float s = menuScale(), width = ImGui::GetWindowWidth(), height = ImGui::GetWindowHeight();
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const UiFonts& fonts = uiFonts();
    const float left = width * 0.07f, right = width * 0.93f;
    menuScreenTitle("Practice", s);
    std::string part = partName(practice.chart, practice.part);
    draw->AddText(fonts.text, 18 * s, ImVec2(left, height * 0.09f + 52 * s), uiColor(UiColor::Dim),
                  (practice.song.title + (part.empty() ? "" : "  ·  " + part)).c_str());

    // The section: chosen on the timeline, or moved with the arrows (Shift: its end)
    const bool typing = ImGui::GetIO().WantTextInput;
    if (!typing && ImGui::IsKeyPressed(ImGuiKey_LeftArrow)){
        if (ImGui::GetIO().KeyShift) practice.toBar = std::max(practice.fromBar + 1, practice.toBar - 1);
        else if (practice.fromBar > 0){ practice.fromBar--; practice.toBar--; }
    }
    if (!typing && ImGui::IsKeyPressed(ImGuiKey_RightArrow)){
        if (ImGui::GetIO().KeyShift) practice.toBar = std::min(practice.bars, practice.toBar + 1);
        else if (practice.toBar < practice.bars){ practice.fromBar++; practice.toBar++; }
    }
    const float timelineTop = height * 0.22f;
    drawTimeline(ImVec2(left, timelineTop), ImVec2(right, timelineTop + 130 * s), s);
    const int fromTick = barStartTick(practice.chart, practice.fromBar), toTick = barStartTick(practice.chart, practice.toBar);
    int count = 0;
    for (const FrettedNote& note : practice.notes) count += note.tick >= fromTick && note.tick < toTick;
    std::string section = practice.toBar - practice.fromBar == 1 ? TextFormat("BAR %d", practice.fromBar + 1)
                                                                  : TextFormat("BARS %d-%d", practice.fromBar + 1, practice.toBar);
    section += "   " + clock(tickToSeconds(practice.chart, fromTick)) + " - " + clock(tickToSeconds(practice.chart, toTick));
    section += TextFormat("   %d NOTES", count);
    draw->AddText(fonts.mono, 14 * s, ImVec2(left, timelineTop + 138 * s), uiColor(UiColor::Accent), section.c_str());
    draw->AddText(fonts.mono, 13 * s, ImVec2(left, timelineTop + 158 * s), uiColor(UiColor::Dim),
                  "Drag across bars to choose them, or drag the section's edges.   Left/Right  move it    Shift + Left/Right  its length");

    // How it's practised
    const float cardTop = timelineTop + 186 * s, cardBottom = height - 64 * s, cardWidth = (right - left) * 0.74f;
    ImGui::SetCursorScreenPos(ImVec2(left, cardTop));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, uiColorVec(UiColor::Card));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 10 * s);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(28 * s, 8 * s));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
    ImGui::BeginChild("PracticeHow", ImVec2(cardWidth, cardBottom - cardTop), ImGuiChildFlags_AlwaysUseWindowPadding | ImGuiChildFlags_NavFlattened);
    settingSegments("Tempo", "Slower, the song keeps its pitch", &practice.tempo, { "40%", "50%", "60%", "70%", "80%", "90%", "100%" });
    ImGui::BeginDisabled(practice.tempo == 6);
    settingToggle("Faster each time", "After a pass with every note played, the next is faster, up to the song's own tempo", &practice.gradual);
    ImGui::BeginDisabled(!practice.gradual);
    settingSegments("By", nullptr, &practice.step, { "+5%", "+10%" });
    ImGui::EndDisabled();
    ImGui::EndDisabled();
    settingToggle("Note by note", "The song waits on each note until you play it", &practice.noteByNote);
    settingSegments("Repeat", "How many times the section is played. Until 100%: until every note is played, at the tempo aimed for",
                    &practice.repeat, { "Until 100%", "3 times", "5 times", "10 times", "Other" });
    if (REPEATS[practice.repeat] < 0) settingSliderInt("Times", "Any number, 1 to 100", &practice.ownRepeats, 1, 100, "%d times");
    ImGui::EndChild();
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor();

    // How the last practice went, and Start
    const float sideX = left + cardWidth + 24 * s;
    if (!practice.message.empty()){
        ImGui::SetCursorScreenPos(ImVec2(sideX, cardTop + 6 * s));
        ImGui::PushTextWrapPos(right);
        ImGui::PushFont(fonts.bold, 18 * s);
        ImGui::TextColored(uiColorVec(UiColor::Good), "%s", practice.message.c_str());
        ImGui::PopFont();
        ImGui::PopTextWrapPos();
    }
    if (menuPill("Start practice", "Enter", ImVec2(right, cardBottom - 30 * s), true, 0, s)) choice = PracticeChoice::Start;
    if (!typing && (ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter))) choice = PracticeChoice::Start;
    menuScreenHint("Enter  start    Esc  back", s);
    ImGui::End();
    return choice;
}
