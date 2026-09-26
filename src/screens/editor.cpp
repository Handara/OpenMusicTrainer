#include "screens/editor.h"

#include "audio/audio.h"
#include "core/chart.h"
#include "core/music.h"
#include "imgui.h"
#include "imgui_stdlib.h"
#include "raylib.h"

#include <algorithm>
#include <cmath>
#include <filesystem>

namespace fs = std::filesystem;

const float SIDE_PANEL_WIDTH = 380.0f;
const float RULER_HEIGHT = 28.0f;
const float LABEL_WIDTH = 64.0f;
const float MAX_ROW_HEIGHT = 64.0f;
const float NOTE_RADIUS = 14.0f;
const float MIN_PIXELS_PER_BEAT = 20.0f;
const float MAX_PIXELS_PER_BEAT = 800.0f;
const float MIN_GRID_LINE_SPACING = 6.0f; // closer grid lines than this (in pixels) are hidden, only beats remain
const int BEATS_PER_BAR = 4;              // the chart format has no time signatures yet: assume 4/4

// Grid choices: snap positions per beat (a beat is a quarter note). 3, 6 and 12 give triplets.
const int SNAP_DIVISIONS[] = { 1, 2, 3, 4, 6, 8, 12, 16 };
const char* const SNAP_LABELS[] = { "1/4", "1/8", "1/8 triplet", "1/16", "1/16 triplet", "1/32", "1/32 triplet", "1/64" };
const int SNAP_CHOICES = sizeof(SNAP_DIVISIONS) / sizeof(SNAP_DIVISIONS[0]);

// Same colors as the gameplay lanes (raylib's RED, ORANGE, GOLD, GREEN, SKYBLUE, PURPLE)
const ImU32 STRING_COLORS[] = { IM_COL32(230, 41, 55, 255), IM_COL32(255, 161, 0, 255), IM_COL32(255, 203, 0, 255),
                                IM_COL32(0, 228, 48, 255), IM_COL32(102, 191, 255, 255), IM_COL32(200, 122, 255, 255) };
const ImU32 COLOR_TIMELINE_BG = IM_COL32(25, 16, 12, 255);
const ImU32 COLOR_TEXT = IM_COL32(230, 210, 190, 255);
const ImU32 COLOR_BAR_LINE = IM_COL32(255, 220, 180, 150);
const ImU32 COLOR_BEAT_LINE = IM_COL32(255, 220, 180, 70);
const ImU32 COLOR_SUBDIVISION_LINE = IM_COL32(255, 220, 180, 25);
const ImU32 COLOR_STRING_LINE = IM_COL32(255, 255, 255, 40);
const ImU32 COLOR_END = IM_COL32(230, 60, 50, 255);

const char* const UNSAVED_POPUP = "Unsaved changes";

struct EditorState {
    Chart chart;
    std::string chartPath;
    std::string songFolder;
    std::string userSongsDir;
    bool builtIn = false;
    bool lowStringOnTop = true; // string order setting: which row each string is drawn in
    bool dirty = false;       // changed since the last save
    std::string status;       // last save result or hint, shown in the top bar

    // View: which part of the song is on screen
    double viewStartTick = 0.0;
    float pixelsPerBeat = 120.0f;

    // Editing
    int snapIndex = 3;        // 1/16 notes
    int newNoteFret = 0;
    bool previewSounds = true; // play a note's pitch when it's placed, clicked or re-fretted
    bool hasSelection = false;
    int selectedTick = 0;     // a note is identified by (tick, string): indices change as notes are added
    int selectedString = 0;

    bool active = false;
};
static EditorState editor;

// --- Note editing -------------------------------------------------------------------------------------
// Notes stay sorted by (tick, string), the order the loader produces, so lookups can use binary search.

static FrettedTrack& track(){
    return editor.chart.frettedTracks[0];
}

static bool noteBefore(const FrettedNote& note, int tick, int stringIndex){
    return note.tick < tick || (note.tick == tick && note.stringIndex < stringIndex);
}

static std::vector<FrettedNote>::iterator findNote(int tick, int stringIndex){
    std::vector<FrettedNote>& notes = track().notes;
    auto it = std::lower_bound(notes.begin(), notes.end(), 0, [&](const FrettedNote& note, int){
        return noteBefore(note, tick, stringIndex);
    });
    if (it != notes.end() && it->tick == tick && it->stringIndex == stringIndex) return it;
    return notes.end();
}

static FrettedNote* selectedNote(){
    if (!editor.hasSelection) return nullptr;
    auto it = findNote(editor.selectedTick, editor.selectedString);
    return it != track().notes.end() ? &*it : nullptr;
}

static void select(int tick, int stringIndex){
    editor.hasSelection = true;
    editor.selectedTick = tick;
    editor.selectedString = stringIndex;
}

static void addNote(int tick, int stringIndex, int fret){
    std::vector<FrettedNote>& notes = track().notes;
    auto it = std::lower_bound(notes.begin(), notes.end(), 0, [&](const FrettedNote& note, int){
        return noteBefore(note, tick, stringIndex);
    });
    if (it != notes.end() && it->tick == tick && it->stringIndex == stringIndex) it->fret = fret;
    else notes.insert(it, FrettedNote{tick, stringIndex, fret, 0});

    // The chart must end after its last note
    int beatsPerBar = editor.chart.resolution * BEATS_PER_BAR;
    if (tick >= editor.chart.endTick) editor.chart.endTick = (tick / beatsPerBar + 1) * beatsPerBar;
    select(tick, stringIndex);
    editor.dirty = true;
}

static void deleteNote(int tick, int stringIndex){
    auto it = findNote(tick, stringIndex);
    if (it == track().notes.end()) return;
    track().notes.erase(it);
    if (editor.hasSelection && editor.selectedTick == tick && editor.selectedString == stringIndex) editor.hasSelection = false;
    editor.dirty = true;
}

// Lets you hear what you're placing: the string's open pitch plus the fret
static void previewNote(int stringIndex, int fret){
    if (editor.previewSounds) playPreview(midiToFrequency((float)(track().tuning[stringIndex] + fret)));
}

static int snapStep(){
    return std::max(1, editor.chart.resolution / SNAP_DIVISIONS[editor.snapIndex]);
}

// --- Saving -------------------------------------------------------------------------------------------

static bool saveEditorChart(){
    if (editor.builtIn){
        // Built-in songs ship with the game and are read-only, so the first save makes an editable copy
        fs::path source = editor.songFolder;
        fs::path destination = fs::path(editor.userSongsDir) / source.filename();
        for (int n = 2; fs::exists(destination); n++){
            destination = fs::path(editor.userSongsDir) / (source.filename().string() + " (" + std::to_string(n) + ")");
        }
        std::error_code ec;
        fs::create_directories(destination, ec);
        if (!ec && !editor.chart.audioFile.empty()){
            fs::copy_file(source / editor.chart.audioFile, destination / editor.chart.audioFile, ec);
        }
        if (ec){
            editor.status = "Could not copy the song: " + ec.message();
            return false;
        }
        editor.songFolder = destination.string();
        editor.chartPath = (destination / "song.chart").string();
        editor.builtIn = false;
    }

    std::string error;
    if (!saveChart(editor.chartPath, editor.chart, error)){
        editor.status = error;
        return false;
    }
    editor.dirty = false;
    editor.status = "Saved to your songs: " + fs::path(editor.songFolder).filename().string();
    return true;
}

// --- Side panel ---------------------------------------------------------------------------------------

static void drawSidePanel(){
    Chart& chart = editor.chart;
    ImGui::PushItemWidth(-170); // leave room for labels on the right

    ImGui::SeparatorText("Song");
    if (ImGui::InputText("Title", &chart.title)) editor.dirty = true;
    if (ImGui::InputText("Artist", &chart.artist)) editor.dirty = true;
    ImGui::TextDisabled("Audio: %s", chart.audioFile.c_str());

    ImGui::SeparatorText("Timing");
    double bpm = chart.tempoMap[0].bpm;
    if (ImGui::InputDouble("BPM", &bpm, 1.0, 10.0, "%.3f") && bpm >= 1.0 && bpm <= 1000.0){
        chart.tempoMap[0].bpm = bpm;
        editor.dirty = true;
    }
    if (chart.tempoMap.size() > 1) ImGui::TextDisabled("+ %d tempo changes", (int)chart.tempoMap.size() - 1);
    if (ImGui::InputDouble("Offset (s)", &chart.offset, 0.001, 0.01, "%.3f")) editor.dirty = true;

    int ticksPerBar = chart.resolution * BEATS_PER_BAR;
    int bars = (chart.endTick + ticksPerBar - 1) / ticksPerBar;
    if (ImGui::InputInt("Length (bars)", &bars)){
        int lastNoteTick = track().notes.empty() ? 0 : track().notes.back().tick;
        chart.endTick = std::max(std::max(bars, 1) * ticksPerBar, (lastNoteTick / ticksPerBar + 1) * ticksPerBar);
        editor.dirty = true;
    }

    ImGui::SeparatorText("Notes");
    ImGui::Combo("Grid", &editor.snapIndex, SNAP_LABELS, SNAP_CHOICES);
    ImGui::SliderInt("New note fret", &editor.newNoteFret, 0, MAX_FRET);
    ImGui::Checkbox("Hear notes", &editor.previewSounds);
    ImGui::SliderFloat("Zoom", &editor.pixelsPerBeat, MIN_PIXELS_PER_BEAT, MAX_PIXELS_PER_BEAT, "%.0f px/beat",
                       ImGuiSliderFlags_Logarithmic);
    if (const FrettedNote* note = selectedNote()){
        int pitch = track().tuning[note->stringIndex] + note->fret;
        ImGui::Text("Selected: bar %d, fret %d (%s%d)", note->tick / ticksPerBar + 1, note->fret,
                    pitchClassName(pitch), pitchOctave(pitch));
    }
    ImGui::Text("%d notes", (int)track().notes.size());
    ImGui::PopItemWidth();

    ImGui::SeparatorText("Controls");
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextWrapped("Left click: add / select note\nRight click: delete note\nUp / Down: fret of selected note "
                       "(or of new notes)\nDelete: delete selected\nWheel: scroll    Ctrl + wheel: zoom\n"
                       "Ctrl + S: save    Esc: back");
    ImGui::PopStyleColor();
}

// --- Timeline -----------------------------------------------------------------------------------------
// A custom ImGui widget: an invisible button reserves the area and captures the mouse,
// and everything visible is drawn by hand with the window's draw list.

static void drawTimeline(){
    ImGuiIO& io = ImGui::GetIO();
    const FrettedTrack& t = track();
    const int resolution = editor.chart.resolution;
    const int stringCount = (int)t.tuning.size();

    ImVec2 origin = ImGui::GetCursorScreenPos();
    ImVec2 size = ImGui::GetContentRegionAvail();
    ImGui::InvisibleButton("timeline", size, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
    bool hovered = ImGui::IsItemHovered();

    float gridLeft = origin.x + LABEL_WIDTH;
    float gridRight = origin.x + size.x;
    float rowsTop = origin.y + RULER_HEIGHT;
    float rowHeight = std::min(MAX_ROW_HEIGHT, (size.y - RULER_HEIGHT) / stringCount);
    auto tickToX = [&](double tick){ return gridLeft + (float)((tick - editor.viewStartTick) / resolution * editor.pixelsPerBeat); };
    auto xToTick = [&](float x){ return editor.viewStartTick + (x - gridLeft) / editor.pixelsPerBeat * resolution; };
    // String <-> screen row, following the string order setting (the mapping is its own inverse)
    auto stringToRow = [&](int stringIndex){ return editor.lowStringOnTop ? stringIndex : stringCount - 1 - stringIndex; };
    auto rowY = [&](int stringIndex){ return rowsTop + (stringToRow(stringIndex) + 0.5f) * rowHeight; };

    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(origin, ImVec2(gridRight, origin.y + size.y), COLOR_TIMELINE_BG);
    draw->PushClipRect(origin, ImVec2(gridRight, origin.y + size.y), true);

    // Grid: bar lines strongest, then beats, then the snap subdivisions (hidden when too close together)
    int step = snapStep();
    if (editor.pixelsPerBeat / SNAP_DIVISIONS[editor.snapIndex] < MIN_GRID_LINE_SPACING) step = resolution;
    double viewEndTick = xToTick(gridRight);
    int ticksPerBar = resolution * BEATS_PER_BAR;
    for (int tick = (int)(editor.viewStartTick / step) * step; tick <= viewEndTick; tick += step){
        float x = tickToX(tick);
        bool isBar = tick % ticksPerBar == 0;
        ImU32 color = isBar ? COLOR_BAR_LINE : (tick % resolution == 0 ? COLOR_BEAT_LINE : COLOR_SUBDIVISION_LINE);
        draw->AddLine(ImVec2(x, rowsTop), ImVec2(x, origin.y + size.y), color, isBar ? 2.0f : 1.0f);
        if (isBar) draw->AddText(ImVec2(x + 4, origin.y + 4), COLOR_TEXT, TextFormat("%d", tick / ticksPerBar + 1));
    }

    // Strings, labeled with their open-string note
    for (int s = 0; s < stringCount; s++){
        draw->AddLine(ImVec2(gridLeft, rowY(s)), ImVec2(gridRight, rowY(s)), COLOR_STRING_LINE);
        draw->AddText(ImVec2(origin.x + 10, rowY(s) - 9), COLOR_TEXT,
                      TextFormat("%s%d", pitchClassName(t.tuning[s]), pitchOctave(t.tuning[s])));
    }

    // End of the chart, with everything after it shaded
    float endX = tickToX(editor.chart.endTick);
    draw->AddRectFilled(ImVec2(std::max(endX, gridLeft), rowsTop), ImVec2(gridRight, origin.y + size.y), IM_COL32(0, 0, 0, 90));
    draw->AddLine(ImVec2(endX, rowsTop), ImVec2(endX, origin.y + size.y), COLOR_END, 2.0f);

    // Notes: they're sorted by tick, so binary-search the first visible one and stop after the last
    double margin = NOTE_RADIUS / editor.pixelsPerBeat * resolution;
    auto first = std::lower_bound(t.notes.begin(), t.notes.end(), editor.viewStartTick - margin,
                                  [](const FrettedNote& note, double tick){ return note.tick < tick; });
    const FrettedNote* hoveredNote = nullptr;
    ImVec2 mouse = io.MousePos;
    for (auto it = first; it != t.notes.end() && it->tick <= viewEndTick + margin; ++it){
        ImVec2 center(tickToX(it->tick), rowY(it->stringIndex));
        if (it->duration > 0){
            draw->AddRectFilled(ImVec2(center.x, center.y - 4), ImVec2(tickToX(it->tick + it->duration), center.y + 4),
                                STRING_COLORS[it->stringIndex % 6] & IM_COL32(255, 255, 255, 120), 3.0f);
        }
        bool isSelected = editor.hasSelection && it->tick == editor.selectedTick && it->stringIndex == editor.selectedString;
        draw->AddCircleFilled(center, NOTE_RADIUS, STRING_COLORS[it->stringIndex % 6]);
        draw->AddCircle(center, NOTE_RADIUS + (isSelected ? 3.0f : 0.0f), IM_COL32_WHITE, 0, isSelected ? 3.0f : 1.5f);
        const char* fret = TextFormat("%d", it->fret);
        ImVec2 textSize = ImGui::CalcTextSize(fret);
        draw->AddText(ImVec2(center.x - textSize.x / 2, center.y - textSize.y / 2), IM_COL32_BLACK, fret);

        float dx = mouse.x - center.x, dy = mouse.y - center.y;
        if (hovered && dx * dx + dy * dy <= NOTE_RADIUS * NOTE_RADIUS) hoveredNote = &*it;
    }

    // Mouse: where a new note would go (snapped to the grid), and clicks
    int hoverRow = (int)std::floor((mouse.y - rowsTop) / rowHeight);
    bool overGrid = hovered && mouse.x >= gridLeft && hoverRow >= 0 && hoverRow < stringCount;
    int hoverString = stringToRow(hoverRow);
    int snappedTick = std::max(0, (int)std::lround(xToTick(mouse.x) / snapStep()) * snapStep());
    if (overGrid && !hoveredNote){
        ImVec2 ghost(tickToX(snappedTick), rowY(hoverString));
        draw->AddCircleFilled(ghost, NOTE_RADIUS, (STRING_COLORS[hoverString % 6] & IM_COL32(255, 255, 255, 0)) | IM_COL32(0, 0, 0, 90));
        const char* fret = TextFormat("%d", editor.newNoteFret);
        ImVec2 textSize = ImGui::CalcTextSize(fret);
        draw->AddText(ImVec2(ghost.x - textSize.x / 2, ghost.y - textSize.y / 2), IM_COL32(0, 0, 0, 160), fret);
    }
    draw->PopClipRect();

    if (ImGui::IsItemClicked(ImGuiMouseButton_Left)){
        if (hoveredNote){
            select(hoveredNote->tick, hoveredNote->stringIndex);
            previewNote(hoveredNote->stringIndex, hoveredNote->fret);
        } else if (overGrid){
            addNote(snappedTick, hoverString, editor.newNoteFret);
            previewNote(hoverString, editor.newNoteFret);
        }
    }
    if (ImGui::IsItemClicked(ImGuiMouseButton_Right) && hoveredNote){
        deleteNote(hoveredNote->tick, hoveredNote->stringIndex); // hoveredNote is invalid after this
    }

    // Wheel scrolls through the song; Ctrl + wheel zooms, keeping the point under the mouse still
    if (hovered && io.MouseWheel != 0.0f){
        if (io.KeyCtrl){
            double tickAtMouse = xToTick(mouse.x);
            editor.pixelsPerBeat = std::clamp(editor.pixelsPerBeat * std::pow(1.15f, io.MouseWheel),
                                              MIN_PIXELS_PER_BEAT, MAX_PIXELS_PER_BEAT);
            editor.viewStartTick = tickAtMouse - (mouse.x - gridLeft) / editor.pixelsPerBeat * resolution;
        } else {
            editor.viewStartTick -= io.MouseWheel * resolution;
        }
        editor.viewStartTick = std::max(0.0, editor.viewStartTick);
    }
}

// Keyboard editing, skipped while a text field is being typed in
static void handleEditingKeys(){
    if (ImGui::GetIO().WantTextInput) return;
    FrettedNote* note = selectedNote();
    int fretChange = (ImGui::IsKeyPressed(ImGuiKey_UpArrow) ? 1 : 0) - (ImGui::IsKeyPressed(ImGuiKey_DownArrow) ? 1 : 0);
    if (fretChange != 0){
        if (note){
            note->fret = std::clamp(note->fret + fretChange, 0, MAX_FRET);
            editor.newNoteFret = note->fret; // keep placing notes at the fret just set
            editor.dirty = true;
            previewNote(note->stringIndex, note->fret);
        } else {
            editor.newNoteFret = std::clamp(editor.newNoteFret + fretChange, 0, MAX_FRET);
        }
    }
    if (note && (ImGui::IsKeyPressed(ImGuiKey_Delete) || ImGui::IsKeyPressed(ImGuiKey_Backspace))){
        deleteNote(note->tick, note->stringIndex);
    }
}

// --- Screen -------------------------------------------------------------------------------------------

bool openEditor(const SongEntry& song, const std::string& userSongsDir, bool lowStringOnTop, std::string& error){
    closeEditor();
    EditorState fresh;
    if (!loadChart(song.chartPath, fresh.chart, error)) return false;
    fresh.chartPath = song.chartPath;
    fresh.songFolder = song.folder;
    fresh.userSongsDir = userSongsDir;
    fresh.builtIn = song.builtIn;
    if (song.builtIn) fresh.status = "Built-in song: saving creates your own copy";
    fresh.lowStringOnTop = lowStringOnTop;
    fresh.active = true;
    editor = fresh;

    // Arrow keys edit notes here instead of moving between buttons
    ImGui::GetIO().ConfigFlags &= ~ImGuiConfigFlags_NavEnableKeyboard;
    return true;
}

void closeEditor(){
    if (!editor.active) return;
    ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    editor.active = false;
}

EditorChoice editorScreen(){
    EditorChoice choice = EditorChoice::None;
    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGui::Begin("Editor", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings
                                    | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollWithMouse);

    // Top bar
    bool requestBack = false;
    if (ImGui::Button("Save")) saveEditorChart();
    ImGui::SameLine();
    if (ImGui::Button("Back")) requestBack = true;
    ImGui::SameLine();
    ImGui::Text("%s%s", editor.chart.title.c_str(), editor.dirty ? "  *" : "");
    ImGui::SameLine();
    ImGui::TextDisabled("%s", editor.status.c_str());
    ImGui::Separator();

    ImGui::BeginChild("Side", ImVec2(SIDE_PANEL_WIDTH, 0));
    drawSidePanel();
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("Timeline", ImVec2(0, 0), ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoScrollbar);
    drawTimeline();
    ImGui::EndChild();

    bool popupOpen = ImGui::IsPopupOpen(UNSAVED_POPUP);
    if (!popupOpen){
        handleEditingKeys();
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S)) saveEditorChart();
        if (ImGui::IsKeyPressed(ImGuiKey_Escape) && !io.WantTextInput) requestBack = true;
    }
    if (requestBack){
        if (editor.dirty) ImGui::OpenPopup(UNSAVED_POPUP);
        else choice = EditorChoice::Back;
    }

    // Leaving with unsaved changes asks first: losing someone's work is the worst thing an editor can do
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal(UNSAVED_POPUP, nullptr, ImGuiWindowFlags_AlwaysAutoResize)){
        ImGui::Text("Save your changes before leaving?");
        if (ImGui::Button("Save")){
            if (saveEditorChart()) choice = EditorChoice::Back;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Discard")){
            choice = EditorChoice::Back;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    ImGui::End();
    return choice;
}
