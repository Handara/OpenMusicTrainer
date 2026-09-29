#include "screens/editor.h"

#include "audio/audio.h"
#include "core/chart.h"
#include "core/files.h"
#include "core/songpackage.h"
#include "core/music.h"
#include "imgui.h"
#include "imgui_stdlib.h"
#include "raylib.h"
#include "ui/theme.h"
#include "ui/ui.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <filesystem>
#include <thread>

namespace fs = std::filesystem;

const float SIDE_PANEL_WIDTH = 380.0f;
const float RULER_HEIGHT = 28.0f;
const float LABEL_WIDTH = 64.0f;
const float MAX_ROW_HEIGHT = 64.0f;
const float NOTE_RADIUS = 14.0f;
const float MIN_PIXELS_PER_BEAT = 20.0f;
const float MAX_PIXELS_PER_BEAT = 800.0f;
const float MIN_GRID_LINE_SPACING = 6.0f; // closer grid lines than this (in pixels) are hidden, only beats remain

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
const ImU32 COLOR_WAVEFORM = IM_COL32(255, 220, 180, 34);

const char* const UNSAVED_POPUP = "Unsaved changes";
const double LOOKAHEAD_S = 0.2;       // playback schedules clicks and notes this far ahead, on the audio clock
const float FOLLOW_AT = 0.5f;
const size_t MAX_UNDO_STEPS = 200;    // a chart is small (a few thousand notes at most): 200 copies is little memory         // while playing, the view scrolls to keep the playhead this far across

struct EditorState {
    Chart chart;
    std::string chartPath;
    std::string songFolder;
    std::string userSongsDir;
    std::string packagesDir;  // where Share writes the song's package
    bool builtIn = false;
    bool lowStringOnTop = true; // string order setting: which row each string is drawn in
    bool dirty = false;       // changed since the last save

    // Undo: whole charts, as they were before each change. A change is committed at the end of a frame when no
    // widget is held, so a slider drag or a word typed is one step, not one per frame.
    std::vector<Chart> undoStack, redoStack;
    Chart committed;          // the chart as of the last committed step
    bool uncommitted = false; // changed since then
    std::string status;       // last save result or hint, shown in the top bar

    // View: which part of the song is on screen
    double viewStartTick = 0.0;
    float pixelsPerBeat = 120.0f;

    // Editing
    int part = 0;             // which fretted track is being edited
    int snapIndex = 3;        // 1/16 notes
    int newNoteFret = 0;
    bool previewSounds = true; // play a note's pitch when it's placed, clicked or re-fretted
    bool hasSelection = false;
    int selectedTick = 0;     // a note is identified by (tick, string): indices change as notes are added
    int selectedString = 0;

    // Playback (Space): the song, a metronome and the chart's notes, from the playhead. Everything runs on the
    // engine's clock: the song is started at a known time on it and every click and note is scheduled against it.
    bool songLoaded = false;   // the chart's audio; without it, playback is the clicks and notes alone
    bool playing = false;
    bool metronome = true;
    bool playNotes = true;
    int playheadTick = 0;      // where playback starts, and where it comes back to when stopped
    double playFrom = 0.0;     // the song time at the playhead when playback started
    double playStartTime = 0.0;// the engine time (audioTime) at which playFrom plays
    int scheduledTick = 0;     // clicks and notes before this tick are already scheduled

    bool active = false;
};
static EditorState editor;

// The song's waveform, worked out on a thread of its own (decoding a long song takes a moment) while the editor is
// already usable; it's drawn once `ready` says so. Kept apart from EditorState, which is copied whole on open.
const int PEAKS_PER_SECOND = 200;
static struct {
    std::thread thread;
    std::atomic<bool> ready{false};
    std::atomic<bool> cancel{false};
    std::vector<float> peaks;    // written by the thread, read only once `ready` is set
    bool show = true;
} waveform;

static void stopWaveform(){
    waveform.cancel = true;
    if (waveform.thread.joinable()) waveform.thread.join();
    waveform.ready = false;
    waveform.cancel = false;
    waveform.peaks.clear();
}

static void startWaveform(const std::string& audioPath){
    stopWaveform();
    waveform.thread = std::thread([audioPath](){
        std::vector<float> peaks;
        if (songPeaks(audioPath, PEAKS_PER_SECOND, peaks, waveform.cancel)){
            waveform.peaks = std::move(peaks);
            waveform.ready = true; // after the peaks are in place: the main thread reads them once it sees this
        }
    });
}

static void markChanged(){
    editor.dirty = true;
    editor.uncommitted = true;
}

// --- Undo ---------------------------------------------------------------------------------------------

static void commitChange(){
    if (!editor.uncommitted) return;
    editor.undoStack.push_back(std::move(editor.committed));
    if (editor.undoStack.size() > MAX_UNDO_STEPS) editor.undoStack.erase(editor.undoStack.begin());
    editor.redoStack.clear(); // a new change starts a new future
    editor.committed = editor.chart;
    editor.uncommitted = false;
}

// Moves one step from one stack to the other: undo takes from the undo stack, redo from the redo stack
static void stepHistory(std::vector<Chart>& from, std::vector<Chart>& to){
    commitChange(); // a change still in progress counts as the latest step
    if (from.empty()) return;
    to.push_back(std::move(editor.chart));
    editor.chart = std::move(from.back());
    from.pop_back();
    // Undoing "add part" takes away the part being edited: stay on one that exists
    editor.part = std::min(editor.part, (int)editor.chart.frettedTracks.size() - 1);
    editor.committed = editor.chart;
    editor.dirty = true;
}

// --- Note editing -------------------------------------------------------------------------------------
// Notes stay sorted by (tick, string), the order the loader produces, so lookups can use binary search.

static FrettedTrack& track(){
    return editor.chart.frettedTracks[editor.part];
}

// Standard tunings for a new part, low to high
const std::vector<int> GUITAR_TUNING = { 40, 45, 50, 55, 59, 64 };
const std::vector<int> BASS_TUNING = { 28, 33, 38, 43 };

static void choosePart(int part){
    editor.part = std::clamp(part, 0, (int)editor.chart.frettedTracks.size() - 1);
    editor.hasSelection = false; // a selection is (tick, string) in one part
}

static void addPart(InstrumentType type){
    FrettedTrack part;
    part.type = type;
    part.name = type == InstrumentType::Bass ? "Bass" : "Guitar";
    part.tuning = type == InstrumentType::Bass ? BASS_TUNING : GUITAR_TUNING;
    editor.chart.frettedTracks.push_back(part);
    choosePart((int)editor.chart.frettedTracks.size() - 1);
    markChanged();
}

static void removePart(){
    if (editor.chart.frettedTracks.size() < 2) return; // a song keeps at least one part
    editor.chart.frettedTracks.erase(editor.chart.frettedTracks.begin() + editor.part);
    choosePart(editor.part);
    markChanged();
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

    // The chart must end after its last note: at the end of that note's bar
    if (tick >= editor.chart.endTick) editor.chart.endTick = barStartTick(editor.chart, barNumberAt(editor.chart, tick) + 1);
    select(tick, stringIndex);
    markChanged();
}

static void deleteNote(int tick, int stringIndex){
    auto it = findNote(tick, stringIndex);
    if (it == track().notes.end()) return;
    track().notes.erase(it);
    if (editor.hasSelection && editor.selectedTick == tick && editor.selectedString == stringIndex) editor.hasSelection = false;
    markChanged();
}

// Lets you hear what you're placing: the string's open pitch plus the fret
static void previewNote(int stringIndex, int fret){
    if (editor.previewSounds) playPreview(midiToFrequency((float)(track().tuning[stringIndex] + fret)));
}

static int snapStep(){
    return std::max(1, editor.chart.resolution / SNAP_DIVISIONS[editor.snapIndex]);
}

// --- Playback -----------------------------------------------------------------------------------------

static double playbackSeconds(){
    return editor.playFrom + (audioTime() - editor.playStartTime);
}

static void startPlayback(){
    editor.playFrom = tickToSeconds(editor.chart, editor.playheadTick);
    double start = editor.songLoaded ? playSongFrom(editor.playFrom) : -1.0;
    if (start < 0.0) start = audioTime() + 0.1; // no song: the clicks and notes still need a moment to be scheduled
    editor.playStartTime = start;
    editor.scheduledTick = editor.playheadTick;
    editor.playing = true;
}

static void stopPlayback(){
    if (!editor.playing) return;
    stopSong();
    stopPreviews(); // what was scheduled ahead mustn't play after the stop
    editor.playing = false;
}

// Schedules what falls in the next moment: metronome clicks on each beat (the bar's first accented) and each note's
// pitch, at their exact times on the engine's clock
static void schedulePlayback(){
    const Chart& chart = editor.chart;
    int horizonTick = (int)std::floor(secondsToTick(chart, playbackSeconds() + LOOKAHEAD_S)) + 1;
    if (horizonTick <= editor.scheduledTick) return;
    auto engineTime = [&](int tick){ return editor.playStartTime + (tickToSeconds(chart, tick) - editor.playFrom); };

    if (editor.metronome){
        for (int tick = editor.scheduledTick; tick < horizonTick; ){
            int barStart = barStartTick(chart, barNumberAt(chart, tick));
            int beat = chart.resolution * 4 / timeSignatureAt(chart, tick).beatUnit;
            int next = barStart + (tick - barStart + beat - 1) / beat * beat; // the first beat at or after the tick
            if (next >= horizonTick) break;
            playClickAt(engineTime(next), next == barStartTick(chart, barNumberAt(chart, next)));
            tick = next + 1;
        }
    }
    if (editor.playNotes){
        const std::vector<FrettedNote>& notes = track().notes;
        auto it = std::lower_bound(notes.begin(), notes.end(), editor.scheduledTick,
                                   [](const FrettedNote& note, int tick){ return note.tick < tick; });
        for (; it != notes.end() && it->tick < horizonTick; ++it){
            playPreviewAt(midiToFrequency((float)(track().tuning[it->stringIndex] + it->fret)), engineTime(it->tick));
        }
    }
    editor.scheduledTick = horizonTick;
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

// --- Sharing ------------------------------------------------------------------------------------------

// The song as one file anyone can install (core/songpackage), in the packages folder, which then opens. It's made
// from the saved song, so unsaved changes have to be saved first.
static void shareSong(){
    if (editor.dirty){
        editor.status = "Save first: the package is made from the saved song";
        return;
    }
    std::error_code ec;
    fs::create_directories(editor.packagesDir, ec);
    std::string name = safeFolderName(editor.chart.title);
    fs::path package = fs::path(editor.packagesDir) / ((name.empty() ? "Song" : name) + SONG_PACKAGE_EXTENSION);
    std::string error;
    if (!exportSongPackage(editor.songFolder, package.string(), error)){
        editor.status = "Could not share: " + error;
        return;
    }
    editor.status = "Package ready: " + package.filename().string();
    openFolder(editor.packagesDir);
}

// --- Side panel ---------------------------------------------------------------------------------------

static void drawSidePanel(){
    Chart& chart = editor.chart;
    ImGui::PushItemWidth(-170); // leave room for labels on the right

    ImGui::SeparatorText("Song");
    if (ImGui::InputText("Title", &chart.title)) markChanged();
    if (ImGui::InputText("Artist", &chart.artist)) markChanged();
    ImGui::TextDisabled("Audio: %s", chart.audioFile.c_str());

    // The song's parts: the one being edited, and adding or removing one
    ImGui::SeparatorText("Part");
    auto partLabel = [&](int i){
        const FrettedTrack& part = chart.frettedTracks[i];
        return std::string(TextFormat("%s (%s)", part.name.c_str(), part.type == InstrumentType::Bass ? "bass" : "guitar"));
    };
    if (ImGui::BeginCombo("Editing", partLabel(editor.part).c_str())){
        for (int i = 0; i < (int)chart.frettedTracks.size(); i++){
            if (ImGui::Selectable(partLabel(i).c_str(), i == editor.part)) choosePart(i);
        }
        ImGui::EndCombo();
    }
    if (ImGui::InputText("Part name", &track().name)) markChanged();
    std::string tuning;
    for (int pitch : track().tuning) tuning += TextFormat("%s%s%d", tuning.empty() ? "" : " ", pitchClassName(pitch), pitchOctave(pitch));
    ImGui::TextDisabled("Tuning: %s", tuning.c_str());
    if (ImGui::Button("Add guitar part")) addPart(InstrumentType::Guitar);
    ImGui::SameLine();
    if (ImGui::Button("Add bass part")) addPart(InstrumentType::Bass);
    ImGui::BeginDisabled(chart.frettedTracks.size() < 2); // a song keeps at least one part
    if (ImGui::Button("Remove this part")) removePart();
    ImGui::EndDisabled();

    ImGui::SeparatorText("Timing");
    double bpm = chart.tempoMap[0].bpm;
    if (ImGui::InputDouble("BPM", &bpm, 1.0, 10.0, "%.3f") && bpm >= 1.0 && bpm <= 1000.0){
        chart.tempoMap[0].bpm = bpm;
        markChanged();
    }
    if (chart.tempoMap.size() > 1) ImGui::TextDisabled("+ %d tempo changes", (int)chart.tempoMap.size() - 1);
    if (ImGui::InputDouble("Offset (s)", &chart.offset, 0.001, 0.01, "%.3f")) markChanged();

    int bars = barNumberAt(chart, chart.endTick - 1) + 1;
    if (ImGui::InputInt("Length (bars)", &bars)){
        int lastNoteTick = 0; // of any part
        for (const FrettedTrack& part : chart.frettedTracks) if (!part.notes.empty()) lastNoteTick = std::max(lastNoteTick, part.notes.back().tick);
        int barsNeeded = barNumberAt(chart, lastNoteTick) + 1; // never shorter than the last note's bar
        chart.endTick = barStartTick(chart, std::max(std::max(bars, 1), barsNeeded));
        markChanged();
    }

    // The song's time signature and key. Later changes must stay on bar lines, so with any, these are read-only
    // here (edit them in the file); like the tempo, the number of changes is shown.
    ImGui::SeparatorText("Notation");
    TimeSignatureChange& time = chart.timeSignatures[0];
    ImGui::BeginDisabled(chart.timeSignatures.size() > 1);
    int beats = time.beats;
    if (ImGui::InputInt("Beats per bar", &beats) && beats >= 1 && beats <= 32){
        time.beats = beats;
        markChanged();
    }
    if (ImGui::BeginCombo("Beat unit", TextFormat("1/%d", time.beatUnit))){
        for (int unit = 1; unit <= 32; unit *= 2){
            if ((chart.resolution * 4) % unit != 0) continue; // the resolution can't split a beat that small
            if (ImGui::Selectable(TextFormat("1/%d", unit), unit == time.beatUnit)){
                time.beatUnit = unit;
                markChanged();
            }
        }
        ImGui::EndCombo();
    }
    ImGui::EndDisabled();
    if (chart.timeSignatures.size() > 1) ImGui::TextDisabled("+ %d time signature changes", (int)chart.timeSignatures.size() - 1);

    KeySignature& key = chart.keys[0].key;
    ImGui::BeginDisabled(chart.keys.size() > 1);
    if (ImGui::BeginCombo("Key", keySignatureName(key).c_str())){
        for (int fifths = -7; fifths <= 7; fifths++){
            KeySignature option{fifths, key.minor};
            std::string label = keySignatureName(option);
            if (fifths != 0) label += TextFormat("   %d %s", std::abs(fifths), fifths > 0 ? "sharps" : "flats");
            if (ImGui::Selectable(label.c_str(), fifths == key.fifths)){
                key = option;
                markChanged();
            }
        }
        ImGui::EndCombo();
    }
    // Same signature, the relative key: G major <-> E minor
    if (ImGui::Checkbox("Minor", &key.minor)) markChanged();
    ImGui::EndDisabled();
    if (chart.keys.size() > 1) ImGui::TextDisabled("+ %d key changes", (int)chart.keys.size() - 1);

    ImGui::SeparatorText("Playback");
    ImGui::Checkbox("Metronome", &editor.metronome);
    ImGui::SameLine();
    ImGui::Checkbox("Play the notes", &editor.playNotes);
    ImGui::Checkbox("Waveform", &waveform.show);
    if (waveform.show && editor.songLoaded && !waveform.ready){
        ImGui::SameLine();
        ImGui::TextDisabled("reading the audio...");
    }
    if (!editor.songLoaded) ImGui::TextDisabled("No audio: plays the clicks and notes alone");

    ImGui::SeparatorText("Notes");
    ImGui::Combo("Grid", &editor.snapIndex, SNAP_LABELS, SNAP_CHOICES);
    ImGui::SliderInt("New note fret", &editor.newNoteFret, 0, MAX_FRET);
    ImGui::Checkbox("Hear notes", &editor.previewSounds);
    ImGui::SliderFloat("Zoom", &editor.pixelsPerBeat, MIN_PIXELS_PER_BEAT, MAX_PIXELS_PER_BEAT, "%.0f px/beat",
                       ImGuiSliderFlags_Logarithmic);
    if (const FrettedNote* note = selectedNote()){
        int pitch = track().tuning[note->stringIndex] + note->fret;
        ImGui::Text("Selected: bar %d, fret %d (%s%d)", barNumberAt(chart, note->tick) + 1, note->fret,
                    pitchClassName(pitch), pitchOctave(pitch));
    }
    ImGui::Text("%d notes", (int)track().notes.size());
    ImGui::PopItemWidth();

    ImGui::SeparatorText("Controls");
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextWrapped("Left click: add / select note\nRight click: delete note\nUp / Down: fret of selected note "
                       "(or of new notes)\nDelete: delete selected\nWheel: scroll    Ctrl + wheel: zoom\n"
                       "Space: play / stop    Click the ruler: move the playhead\n"
                       "F5: test play from the playhead\n"
                       "Ctrl + Z: undo    Ctrl + Y: redo\n"
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
    for (int tick = (int)(editor.viewStartTick / step) * step; tick <= viewEndTick; tick += step){
        float x = tickToX(tick);
        int bar = barNumberAt(editor.chart, tick);
        int barStart = barStartTick(editor.chart, bar);
        int beatLength = resolution * 4 / timeSignatureAt(editor.chart, tick).beatUnit; // a beat of 1/8 is half a quarter
        bool isBar = tick == barStart;
        ImU32 color = isBar ? COLOR_BAR_LINE : ((tick - barStart) % beatLength == 0 ? COLOR_BEAT_LINE : COLOR_SUBDIVISION_LINE);
        verticalLine(draw, x, rowsTop, origin.y + size.y, isBar ? 2.0f : 1.0f, color);
        if (isBar) draw->AddText(ImVec2(x + 4, origin.y + 4), COLOR_TEXT, TextFormat("%d", bar + 1));
    }

    // The song's waveform behind the strings, placed by the chart's timing: when the offset and tempo are right,
    // its attacks line up with the beats
    if (waveform.show && waveform.ready && !waveform.peaks.empty()){
        const std::vector<float>& peaks = waveform.peaks;
        float middle = rowsTop + rowHeight * stringCount / 2, halfHeight = rowHeight * stringCount / 2;
        const float column = 2.0f;
        for (float x = gridLeft; x < gridRight; x += column){
            double from = tickToSeconds(editor.chart, (int)xToTick(x)), to = tickToSeconds(editor.chart, (int)xToTick(x + column));
            int first = (int)(from * PEAKS_PER_SECOND), last = std::max(first, (int)(to * PEAKS_PER_SECOND) - 1);
            if (last < 0 || first >= (int)peaks.size()) continue;
            float peak = 0.0f;
            for (int i = std::max(first, 0); i <= last && i < (int)peaks.size(); i++) peak = std::max(peak, peaks[i]);
            draw->AddRectFilled(ImVec2(x, middle - peak * halfHeight), ImVec2(x + column - 0.5f, middle + peak * halfHeight), COLOR_WAVEFORM);
        }
    }

    // Strings, labeled with their open-string note
    for (int s = 0; s < stringCount; s++){
        horizontalLine(draw, gridLeft, gridRight, rowY(s), 1.0f, COLOR_STRING_LINE);
        draw->AddText(ImVec2(origin.x + 10, rowY(s) - 9), COLOR_TEXT,
                      TextFormat("%s%d", pitchClassName(t.tuning[s]), pitchOctave(t.tuning[s])));
    }

    // End of the chart, with everything after it shaded
    float endX = tickToX(editor.chart.endTick);
    draw->AddRectFilled(ImVec2(std::max(endX, gridLeft), rowsTop), ImVec2(gridRight, origin.y + size.y), IM_COL32(0, 0, 0, 90));
    verticalLine(draw, endX, rowsTop, origin.y + size.y, 2.0f, COLOR_END);

    // The playhead: where playback starts, or where it is while playing. Playing, the view follows it.
    int playhead = editor.playing ? (int)secondsToTick(editor.chart, playbackSeconds()) : editor.playheadTick;
    if (editor.playing){
        double visibleTicks = (gridRight - gridLeft) / editor.pixelsPerBeat * resolution;
        if (playhead > editor.viewStartTick + visibleTicks * FOLLOW_AT || playhead < editor.viewStartTick){
            editor.viewStartTick = std::max(0.0, playhead - visibleTicks * FOLLOW_AT);
        }
    }

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
    float playheadX = tickToX(playhead);
    ImU32 accent = uiColor(UiColor::Accent);
    verticalLine(draw, playheadX, origin.y, origin.y + size.y, 2.0f, accent);
    draw->AddTriangleFilled(ImVec2(playheadX - 7, origin.y), ImVec2(playheadX + 7, origin.y), ImVec2(playheadX, origin.y + 10), accent);
    draw->PopClipRect();

    bool overRuler = hovered && mouse.y < rowsTop && mouse.x >= gridLeft;
    if (ImGui::IsItemClicked(ImGuiMouseButton_Left) && overRuler){
        // The ruler moves the playhead (to the grid); playing, playback jumps there
        editor.playheadTick = snappedTick;
        if (editor.playing){
            stopPlayback();
            startPlayback();
        }
    } else if (ImGui::IsItemClicked(ImGuiMouseButton_Left)){
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
    ImGuiIO& io = ImGui::GetIO();
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Z)){
        if (io.KeyShift) stepHistory(editor.redoStack, editor.undoStack);
        else stepHistory(editor.undoStack, editor.redoStack);
    }
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Y)) stepHistory(editor.redoStack, editor.undoStack);
    if (ImGui::IsKeyPressed(ImGuiKey_Space, false)){
        if (editor.playing) stopPlayback();
        else startPlayback();
    }
    FrettedNote* note = selectedNote();
    int fretChange = (ImGui::IsKeyPressed(ImGuiKey_UpArrow) ? 1 : 0) - (ImGui::IsKeyPressed(ImGuiKey_DownArrow) ? 1 : 0);
    if (fretChange != 0){
        if (note){
            note->fret = std::clamp(note->fret + fretChange, 0, MAX_FRET);
            editor.newNoteFret = note->fret; // keep placing notes at the fret just set
            markChanged();
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

bool openEditor(const SongEntry& song, const std::string& userSongsDir, const std::string& packagesDir, bool lowStringOnTop,
                std::string& error){
    closeEditor();
    EditorState fresh;
    if (!loadChart(song.chartPath, fresh.chart, error)) return false;
    if (fresh.chart.frettedTracks.empty()){
        error = "The editor can't edit keys parts yet, and this song has only those";
        return false;
    }
    fresh.chartPath = song.chartPath;
    fresh.songFolder = song.folder;
    fresh.userSongsDir = userSongsDir;
    fresh.packagesDir = packagesDir;
    fresh.builtIn = song.builtIn;
    if (song.builtIn) fresh.status = "Built-in song: saving creates your own copy";
    fresh.lowStringOnTop = lowStringOnTop;
    fresh.active = true;
    fresh.committed = fresh.chart;
    editor = fresh;

    // The song's audio, for playback and its waveform. A chart without it can still be edited and played as clicks
    // and notes.
    std::string audioError;
    stopWaveform();
    if (!editor.chart.audioFile.empty()){
        std::string audioPath = (fs::path(song.folder) / editor.chart.audioFile).string();
        editor.songLoaded = loadSong(audioPath, audioError);
        if (editor.songLoaded) startWaveform(audioPath);
    }

    // Arrow keys edit notes here instead of moving between buttons
    ImGui::GetIO().ConfigFlags &= ~ImGuiConfigFlags_NavEnableKeyboard;
    return true;
}

void closeEditor(){
    if (!editor.active) return;
    stopPlayback();
    unloadSong();
    stopWaveform();
    ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    editor.active = false;
}

EditorTestPlay editorTestPlay(){
    return { editor.chart, (fs::path(editor.songFolder) / editor.chart.audioFile).string(), editor.playheadTick, editor.part };
}

void resumeEditor(const std::string& message){
    if (!message.empty()) editor.status = message;
    std::string error;
    editor.songLoaded = loadSong((fs::path(editor.songFolder) / editor.chart.audioFile).string(), error);
}

EditorChoice editorScreen(){
    EditorChoice choice = EditorChoice::None;
    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGui::Begin("Editor", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings
                                    | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollWithMouse);

    // Top bar
    bool requestBack = false, requestTestPlay = false;
    if (ImGui::Button("Save")) saveEditorChart();
    ImGui::SameLine();
    if (ImGui::Button("Back")) requestBack = true;
    ImGui::SameLine();
    if (ImGui::Button("Test play")) requestTestPlay = true;
    ImGui::SameLine();
    if (ImGui::Button("Share")) shareSong();
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

    if (!ImGui::IsAnyItemActive()) commitChange(); // nothing held: what changed this frame is one undo step
    if (editor.playing){
        schedulePlayback();
        // Past the end of both the chart and the song, playback stops on its own
        double end = std::max(tickToSeconds(editor.chart, editor.chart.endTick), editor.songLoaded ? songLength() : 0.0);
        if (playbackSeconds() > end + 0.5) stopPlayback();
    }

    bool popupOpen = ImGui::IsPopupOpen(UNSAVED_POPUP);
    if (!popupOpen){
        handleEditingKeys();
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S)) saveEditorChart();
        if (ImGui::IsKeyPressed(ImGuiKey_F5)) requestTestPlay = true;
        if (ImGui::IsKeyPressed(ImGuiKey_Escape) && !io.WantTextInput) requestBack = true;
    }
    if (requestTestPlay){
        if (editor.chart.audioFile.empty() || !editor.songLoaded) editor.status = "Test play needs the song's audio";
        else {
            stopPlayback();
            choice = EditorChoice::TestPlay;
        }
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
