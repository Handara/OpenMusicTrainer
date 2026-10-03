#include "screens/practicescreen.h"

#include "audio/audio.h"
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
#include <filesystem>

const int AROUND_BARS = 4;          // from the pause menu: this many bars, from the one it was paused in
const double LISTEN_LEAD_S = 0.5;   // listening, the section is heard from this long before it starts
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
    int endTick = 1;                // the end of the last bar
    int fromTick = 0, toTick = 1;   // the section, on beats: from fromTick up to (not including) toTick
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
    int anchorTick = 0;
    // Listening to the section (Space), at the tempo chosen
    std::string audioPath;
    bool songLoaded = false;
    bool listening = false;
    int heardFrom = 0, heardTo = 0; // the section being heard: listening starts again when it changes
} practice;

// The beat a tick falls in (its start), and the length of a beat there
static int beatLength(int tick){
    return practice.chart.resolution * 4 / std::max(1, timeSignatureAt(practice.chart, tick).beatUnit);
}
static int beatAtOrBefore(int tick){
    const int barStart = barStartTick(practice.chart, barNumberAt(practice.chart, std::max(0, tick)));
    const int beat = beatLength(barStart);
    return barStart + std::max(0, tick - barStart) / beat * beat;
}
// The beat line nearest a tick
static int nearestBeat(double tick){
    const int before = beatAtOrBefore((int)std::floor(tick)), after = before + beatLength(before);
    return tick - before < after - tick ? before : after;
}

// "2.3": bar 2, its third beat; "2" on a bar line
static std::string placeText(int tick){
    const int bar = barNumberAt(practice.chart, tick), start = barStartTick(practice.chart, bar);
    const int beat = (tick - start) / beatLength(start);
    return beat == 0 ? std::to_string(bar + 1) : TextFormat("%d.%d", bar + 1, beat + 1);
}

static void stopListening(){
    if (practice.listening) stopSong();
    practice.listening = false;
}

// The section heard from a moment before it, at the tempo chosen
static void startListening(float speed){
    if (!practice.songLoaded) return;
    stopSong();
    setSongSpeed(speed);
    setSongVolume(1.0f);
    playSongFrom(std::max(0.0, tickToSeconds(practice.chart, practice.fromTick) - LISTEN_LEAD_S * speed));
    practice.listening = true;
    practice.heardFrom = practice.fromTick;
    practice.heardTo = practice.toTick;
}

void closePracticeScreen(){
    stopListening();
    if (practice.songLoaded) unloadSong();
    practice.songLoaded = false;
    setSongSpeed(1.0f);
}

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
    practice.endTick = barStartTick(chart, practice.bars);
    practice.message = message;
    practice.drag = PracticeState::Drag::None;
    auto barsFrom = [&](int bar){
        bar = std::clamp(bar, 0, std::max(0, practice.bars - AROUND_BARS));
        practice.fromTick = barStartTick(chart, bar);
        practice.toTick = barStartTick(chart, std::min(practice.bars, bar + AROUND_BARS));
    };
    if (aroundSeconds >= 0.0) barsFrom(barNumberAt(chart, std::max(0, (int)secondsToTick(chart, aroundSeconds))));
    else if (!sameSong) barsFrom(practice.notes.empty() ? 0 : barNumberAt(chart, practice.notes.front().tick)); // the first bars with notes
    practice.fromTick = std::clamp(practice.fromTick, 0, practice.endTick - 1);
    practice.toTick = std::clamp(practice.toTick, practice.fromTick + 1, practice.endTick);
    // Its audio, to listen to the section before practising it
    if (practice.songLoaded) unloadSong();
    std::string audioError;
    practice.audioPath = (std::filesystem::path(song.folder) / chart.audioFile).string();
    practice.songLoaded = !chart.audioFile.empty() && loadSong(practice.audioPath, audioError);
    practice.listening = false;
    return true;
}

PracticeOptions practiceChoice(){
    PracticeOptions options;
    options.on = true;
    options.fromTick = practice.fromTick;
    options.toTick = practice.toTick;
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
    auto tickAt = [&](float x){ return std::clamp((double)(x - min.x) / width * endTick, 0.0, (double)endTick); };

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
    // Beats too, faintly, where there's room for them
    if (barWidth >= 24 * s){
        for (int bar = 0; bar < practice.bars; bar++){
            const int start = barStartTick(chart, bar), beat = beatLength(start), next = barStartTick(chart, bar + 1);
            for (int tick = start + beat; tick < next; tick += beat) verticalLine(draw, tickX(tick), rowsTop + 3 * s, rowsBottom - 3 * s, 1.0f, uiColor(UiColor::StaffLine, 0.45f));
        }
    }
    const float fromX = tickX(practice.fromTick), toX = tickX(practice.toTick);
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
    // Listening: where the song is
    if (practice.listening){
        const float x = tickX(secondsToTick(chart, songPosition()));
        if (x >= min.x && x <= max.x) verticalLine(draw, x, rowsTop, rowsBottom, 2.0f * s, uiColor(UiColor::Ink));
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
    // The mouse's only: with the keyboard focus on it after a click, Space (listen) or Enter would press it too
    const bool mouseHeld = ImGui::IsMouseDown(ImGuiMouseButton_Left);
    if (ImGui::IsItemActivated() && mouseHeld){
        if (nearTo) practice.drag = PracticeState::Drag::To;
        else if (nearFrom) practice.drag = PracticeState::Drag::From;
        else {
            practice.drag = PracticeState::Drag::Select;
            practice.anchorTick = beatAtOrBefore((int)tickAt(mouseX));
        }
    }
    if (ImGui::IsItemActive() && mouseHeld && practice.drag != PracticeState::Drag::None){
        // Edges go to the nearest beat; a selection covers the beats between the press and the mouse
        const int line = std::clamp(nearestBeat(tickAt(mouseX)), 0, endTick);
        switch (practice.drag){
            case PracticeState::Drag::From: practice.fromTick = std::min(line, practice.toTick - beatLength(practice.toTick - 1)); break;
            case PracticeState::Drag::To: practice.toTick = std::max(line, practice.fromTick + beatLength(practice.fromTick)); break;
            case PracticeState::Drag::Select: {
                const int beat = beatAtOrBefore(std::min((int)tickAt(mouseX), endTick - 1));
                practice.fromTick = std::min(beat, practice.anchorTick);
                const int last = std::max(beat, practice.anchorTick);
                practice.toTick = std::min(endTick, last + beatLength(last));
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

    // The section: chosen on the timeline, or moved a beat with the arrows (Shift: its end)
    const bool typing = ImGui::GetIO().WantTextInput;
    if (!typing && ImGui::IsKeyPressed(ImGuiKey_LeftArrow)){
        if (ImGui::GetIO().KeyShift) practice.toTick = std::max(practice.fromTick + beatLength(practice.fromTick), practice.toTick - beatLength(practice.toTick - 1));
        else if (practice.fromTick > 0){
            const int step = beatLength(practice.fromTick - 1);
            practice.fromTick -= step;
            practice.toTick -= step;
        }
    }
    if (!typing && ImGui::IsKeyPressed(ImGuiKey_RightArrow)){
        const int step = beatLength(practice.toTick);
        if (ImGui::GetIO().KeyShift) practice.toTick = std::min(practice.endTick, practice.toTick + step);
        else if (practice.toTick + step <= practice.endTick){
            practice.fromTick += step;
            practice.toTick += step;
        }
    }
    const float timelineTop = height * 0.22f;
    drawTimeline(ImVec2(left, timelineTop), ImVec2(right, timelineTop + 130 * s), s);
    const int fromTick = practice.fromTick, toTick = practice.toTick;
    int count = 0;
    for (const FrettedNote& note : practice.notes) count += note.tick >= fromTick && note.tick < toTick;
    std::string section = "FROM " + placeText(fromTick) + " TO " + placeText(toTick);
    section += "   " + clock(tickToSeconds(practice.chart, fromTick)) + " - " + clock(tickToSeconds(practice.chart, toTick));
    section += TextFormat("   %d NOTES", count);
    draw->AddText(fonts.mono, 14 * s, ImVec2(left, timelineTop + 138 * s), uiColor(UiColor::Accent), section.c_str());
    draw->AddText(fonts.mono, 13 * s, ImVec2(left, timelineTop + 158 * s), uiColor(UiColor::Dim),
                  "Drag across beats to choose them, or drag the section's edges.   Left/Right  move it    Shift + Left/Right  its length    Space  listen");

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
    // Listening to the section, to check it's the right one: once through, from a moment before it
    const float speed = TEMPOS[practice.tempo] / 100.0f;
    if (practice.listening){
        const double end = tickToSeconds(practice.chart, practice.toTick);
        if (songEnded() || songPosition() > end + 0.25) stopListening();
        // The section changed (once the mouse lets go), or the tempo did: heard again from its start
        else if (practice.drag == PracticeState::Drag::None && (practice.heardFrom != practice.fromTick || practice.heardTo != practice.toTick)) startListening(speed);
        else if (songSpeed() != speed) startListening(speed);
    }
    if (practice.songLoaded && menuPill(practice.listening ? "Stop" : "Listen", "Space", ImVec2(right, cardBottom - 72 * s), true, 0, s)){
        if (practice.listening) stopListening();
        else startListening(speed);
    }
    if (practice.songLoaded && !typing && ImGui::IsKeyPressed(ImGuiKey_Space, false)){
        if (practice.listening) stopListening();
        else startListening(speed);
    }
    if (menuPill("Start practice", "Enter", ImVec2(right, cardBottom - 30 * s), true, 0, s)) choice = PracticeChoice::Start;
    if (!typing && (ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter))) choice = PracticeChoice::Start;
    menuScreenHint("Space  listen    Enter  start    Esc  back", s);
    if (choice == PracticeChoice::Start) closePracticeScreen(); // the play screen loads the song itself
    ImGui::End();
    return choice;
}
