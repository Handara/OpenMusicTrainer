#include "screens/editor.h"

#include "audio/audio.h"
#include "core/chart.h"
#include "core/files.h"
#include "core/songpackage.h"
#include "core/music.h"
#include "imgui.h"
#include "imgui_stdlib.h"
#include "raylib.h"
#include "ui/menulist.h"
#include "ui/settingsui.h"
#include "ui/theme.h"
#include "ui/ui.h"
#include "views/playnote.h"

#include <algorithm>
#include <atomic>
#include <climits>
#include <cmath>
#include <filesystem>
#include <set>
#include <thread>

namespace fs = std::filesystem;

// Sizes at a 720-pixel-tall window
const float TOP_BAR = 56.0f;             // Back, the song, what's done to it
const float TOOL_BAR = 46.0f;            // play, the parts, the grid
const float BOTTOM_BAR = 40.0f;          // what the mouse would do here, the volumes, the zoom
const float BUTTON_HEIGHT = 30.0f;
const float RULER_HEIGHT = 26.0f;
const float LABEL_WIDTH = 58.0f;         // the strings' names, left of the grid
const float MAX_ROW_HEIGHT = 88.0f;      // a string's row, beside the waveform
const float MAX_ROW_HEIGHT_ALONE = 110.0f; // and without it
const float MIN_WAVE_HEIGHT = 64.0f;     // the waveform's own lane, over the strings
const float MAX_WAVE_HEIGHT = 150.0f;
const float NOTE_RADIUS = 15.0f;
const float OVERVIEW_HEIGHT = 34.0f;     // the whole song, small, under the timeline
const float DRAWER_WIDTH = 340.0f;       // the song's details, beside the timeline
const float VIEW_LEAD = 40.0f;           // pixels kept free before the song's start, so its first notes show whole
const float MIN_PIXELS_PER_BEAT = 20.0f;
const float MAX_PIXELS_PER_BEAT = 800.0f;
const float MIN_GRID_LINE_SPACING = 6.0f; // closer grid lines than this (in pixels) are hidden, only beats remain
const float DRAG_STARTS = 4.0f;          // pixels the mouse moves before a click becomes a drag
const float WHEEL_SCROLL = 110.0f;       // pixels a notch of the wheel moves along the song
const double FRET_TYPING_S = 0.8;        // two digits typed this close together are one fret: 1 then 2 is 12
const double SETTLE_S = 0.5;             // wheel notches and key repeats this close together are one undo step
const float PLACED_NOTE_S = 1.0f;        // how long a note rings when it's placed or picked
const double RING_S = 1.2;               // played back, a note with no length rings this long, or until its string's next note
const double MIN_SOUND_S = 0.12;
const int NOTES_MADE_PER_FRAME = 2;      // each note's sound is worked out when it's scheduled: a chord is spread over frames

// Grid choices: snap positions per beat (a beat is a quarter note). 3, 6 and 12 give triplets.
const int SNAP_DIVISIONS[] = { 1, 2, 3, 4, 6, 8, 12, 16 };
const std::vector<std::string> SNAP_LABELS = { "1/4", "1/8", "1/8 triplet", "1/16", "1/16 triplet", "1/32", "1/32 triplet", "1/64" };

const char* const UNSAVED_POPUP = "Unsaved changes";
const char* const KEYS_POPUP = "Editor keys";
const char* const ADD_PART_POPUP = "Add a part";
const double LOOKAHEAD_S = 0.2;       // playback schedules clicks and notes this far ahead, on the audio clock
const float FOLLOW_AT = 0.5f;         // while playing, the view scrolls to keep the playhead this far across
const size_t MAX_UNDO_STEPS = 200;    // a chart is small (a few thousand notes at most): 200 copies is little memory

using NoteKey = std::pair<int, int>;  // a note is identified by (tick, string): indices change as notes are added

// What the mouse is doing on the timeline while a button is held
enum class Drag {
    None,
    Note,      // pressed on a note: a click selects it, moving turns it into Move
    Move,      // the selected notes, along the song and across the strings
    Length,    // how long a note is held: its end follows the mouse
    Box,       // a rectangle selecting the notes inside
    Erase,     // the right button: every note it passes over goes
    Playhead,  // on the ruler or the waveform
    Pan,       // the middle button: the view follows the mouse
};

struct EditorState {
    Chart chart;
    std::string chartPath;
    std::string songFolder;
    std::string userSongsDir;
    std::string packagesDir;  // where Share writes the song's package
    Settings* settings = nullptr; // the string order, and the editor's volumes
    bool builtIn = false;
    bool dirty = false;       // changed since the last save

    // Undo: whole charts, as they were before each change. A change is committed at the end of a frame when no
    // widget is held (and no run of wheel notches or key repeats is under way), so a drag, a word typed or a fret
    // wheeled from 3 to 7 is one step, not one per frame.
    std::vector<Chart> undoStack, redoStack;
    Chart committed;          // the chart as of the last committed step
    bool uncommitted = false; // changed since then
    double settleUntil = 0.0; // GetTime() before which a change isn't committed: more of it may be coming
    std::string status;       // last save result or hint, shown in the top bar

    // View: which part of the song is on screen
    double viewStartTick = 0.0;
    float pixelsPerBeat = 120.0f;
    float gridLeft = 0.0f, gridWidth = 1.0f; // where the timeline's grid was drawn last, in pixels
    bool drawerOpen = false;  // the song's details
    bool openKeys = false;    // the keys' list was asked for: its popup opens this frame

    // Editing
    int part = 0;             // which fretted track is being edited
    int snapIndex = 3;        // 1/16 notes
    int newNoteFret = 0;      // the fret of the next note placed: the wheel, the arrows or the digits change it
    std::set<NoteKey> selection;
    int hoverString = -1;     // the string under the mouse, -1 for none
    float wheelCarry = 0.0f;  // a trackpad's wheel comes in fractions: what hasn't made a whole notch yet
    int typedDigit = -1;      // a fret's first digit, waiting for a second
    double typedAt = 0.0;

    Drag drag = Drag::None;
    ImVec2 dragFrom;          // where the button went down
    double dragFromTick = 0.0; // and where in the song that was: the view may scroll under a drag
    int dragEndTick = 0;      // Move: the song's end then (notes dragged past it push it back; dragged home, it returns)
    bool dragMoved = false;   // it has gone further than DRAG_STARTS since
    NoteKey dragNote;         // the note pressed on (Note, Move, Length)
    std::vector<FrettedNote> dragBase; // Move: the part's notes as the drag began; each frame moves them from there
    std::set<NoteKey> dragSelection;   // Move: what was selected then. Box: what stays selected whatever the box holds
    bool dragChanged = false;
    double panFromTick = 0.0;
    bool resumeAfterDrag = false; // the playhead was grabbed while playing: playback starts again from where it's left

    // Playback (Space): the song, a metronome and the chart's notes, from the playhead. Everything runs on the
    // engine's clock: the song is started at a known time on it and every click and note is scheduled against it.
    bool songLoaded = false;   // the chart's audio; without it, playback is the clicks and notes alone
    bool playing = false;
    bool metronome = true;
    bool playNotes = true;
    int playheadTick = 0;      // where playback starts, and where it comes back to when stopped
    double playFrom = 0.0;     // the song time at the playhead when playback started
    double playStartTime = 0.0;// the engine time (audioTime) at which playFrom plays
    int scheduledTick = 0;     // clicks before this tick are already scheduled
    NoteKey scheduledNote;     // and the part's notes up to this one (a tick and a string: it needn't be a note's)

    bool active = false;
};
static EditorState editor;
static std::vector<FrettedNote> clipboard; // copied notes, their ticks counted from the first one's; kept between songs
static std::string hoverTip;               // what the control under the mouse does, for the bottom bar; set each frame

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

// A change that often comes in runs (a wheel notch, a repeating key): the undo step waits for the run to end
static void markChangedInRun(){
    markChanged();
    editor.settleUntil = GetTime() + SETTLE_S;
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

// --- Note editing -------------------------------------------------------------------------------------
// Notes stay sorted by (tick, string), the order the loader produces, so lookups can use binary search.

static FrettedTrack& track(){
    return editor.chart.frettedTracks[editor.part];
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

static bool isSelected(const FrettedNote& note){
    return editor.selection.count({note.tick, note.stringIndex}) > 0;
}

// The selection names notes by where they are: after anything that may have taken notes away (an undo), the names
// with no note behind them go
static void pruneSelection(){
    for (auto it = editor.selection.begin(); it != editor.selection.end(); ){
        if (findNote(it->first, it->second) == track().notes.end()) it = editor.selection.erase(it);
        else ++it;
    }
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
    editor.drag = Drag::None;
    pruneSelection();
}

// Standard tunings for a new part, low to high
const std::vector<int> GUITAR_TUNING = { 40, 45, 50, 55, 59, 64 };
const std::vector<int> BASS_TUNING = { 28, 33, 38, 43 };

static void choosePart(int part){
    editor.part = std::clamp(part, 0, (int)editor.chart.frettedTracks.size() - 1);
    editor.selection.clear(); // a selection is (tick, string) in one part
    editor.drag = Drag::None;
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

// The chart must end after its last note: at the end of that note's bar
static void coverNotes(){
    const std::vector<FrettedNote>& notes = track().notes;
    if (notes.empty() || notes.back().tick < editor.chart.endTick) return;
    editor.chart.endTick = barStartTick(editor.chart, barNumberAt(editor.chart, notes.back().tick) + 1);
}

static void addNote(int tick, int stringIndex, int fret){
    std::vector<FrettedNote>& notes = track().notes;
    auto it = std::lower_bound(notes.begin(), notes.end(), 0, [&](const FrettedNote& note, int){
        return noteBefore(note, tick, stringIndex);
    });
    if (it != notes.end() && it->tick == tick && it->stringIndex == stringIndex) it->fret = fret;
    else notes.insert(it, FrettedNote{tick, stringIndex, fret, 0});
    coverNotes();
    editor.selection = { {tick, stringIndex} };
    markChanged();
}

static void deleteNote(int tick, int stringIndex){
    auto it = findNote(tick, stringIndex);
    if (it == track().notes.end()) return;
    track().notes.erase(it);
    editor.selection.erase({tick, stringIndex});
    markChanged();
}

static void deleteSelection(){
    if (editor.selection.empty()) return;
    std::vector<FrettedNote>& notes = track().notes;
    notes.erase(std::remove_if(notes.begin(), notes.end(), isSelected), notes.end());
    editor.selection.clear();
    markChanged();
}

// The part's notes become `kept` and `placed` together, a placed note taking the place of a kept one at the same
// tick and string; the placed ones are then the selection
static void putNotes(const std::vector<FrettedNote>& kept, const std::vector<FrettedNote>& placed){
    std::set<NoteKey> landed;
    for (const FrettedNote& note : placed) landed.insert({note.tick, note.stringIndex});
    std::vector<FrettedNote> notes;
    for (const FrettedNote& note : kept) if (!landed.count({note.tick, note.stringIndex})) notes.push_back(note);
    notes.insert(notes.end(), placed.begin(), placed.end());
    std::sort(notes.begin(), notes.end(), [](const FrettedNote& a, const FrettedNote& b){ return noteBefore(a, b.tick, b.stringIndex); });
    track().notes = notes;
    editor.selection = landed;
    coverNotes();
}

// `base` (the part's notes) with those in `selected` moved by a number of ticks and strings, stopped at the song's
// start and at the outer strings. True if they moved at all.
static bool moveNotes(const std::vector<FrettedNote>& base, const std::set<NoteKey>& selected, int ticks, int strings){
    int firstTick = INT_MAX, lowest = INT_MAX, highest = INT_MIN;
    for (const NoteKey& key : selected){
        firstTick = std::min(firstTick, key.first);
        lowest = std::min(lowest, key.second);
        highest = std::max(highest, key.second);
    }
    if (selected.empty()) return false;
    ticks = std::max(ticks, -firstTick);
    strings = std::clamp(strings, -lowest, (int)track().tuning.size() - 1 - highest);
    std::vector<FrettedNote> kept, moved;
    for (const FrettedNote& note : base){
        if (!selected.count({note.tick, note.stringIndex})) kept.push_back(note);
        else moved.push_back({note.tick + ticks, note.stringIndex + strings, note.fret, note.duration});
    }
    putNotes(kept, moved);
    return ticks != 0 || strings != 0;
}

// The selected notes onto the next string up or down, keeping their pitch: the same notes, fingered elsewhere.
// Nothing moves unless every one of them exists on its new string.
static void restringSelection(int strings){
    const std::vector<int>& tuning = track().tuning;
    std::vector<FrettedNote> kept, moved;
    for (const FrettedNote& note : track().notes){
        if (!isSelected(note)){
            kept.push_back(note);
            continue;
        }
        int to = note.stringIndex + strings;
        int fret = to >= 0 && to < (int)tuning.size() ? note.fret + tuning[note.stringIndex] - tuning[to] : -1;
        if (fret < 0 || fret > MAX_FRET){
            editor.status = "That string doesn't have those notes";
            return;
        }
        moved.push_back({note.tick, to, fret, note.duration});
    }
    if (moved.empty()) return;
    putNotes(kept, moved);
    markChangedInRun();
}

// Lets you hear what you're placing: the string's open pitch plus the fret, on the part's own instrument
static void previewNote(int stringIndex, int fret){
    playStringNote(midiToFrequency((float)(track().tuning[stringIndex] + fret)), track().type == InstrumentType::Bass, PLACED_NOTE_S,
                   editor.settings->editorNoteVolume);
}

// The selected notes' frets, moved by `change` or (with `set`) all put on one fret. With nothing selected, it's the
// fret of the next note placed. Either way the result is heard.
static void changeFrets(int change, bool set, int fret){
    std::vector<FrettedNote>& notes = track().notes;
    bool heard = false;
    for (FrettedNote& note : notes){
        if (!isSelected(note)) continue;
        int to = std::clamp(set ? fret : note.fret + change, 0, MAX_FRET);
        if (to != note.fret){
            note.fret = to;
            markChangedInRun();
        }
        if (!heard) previewNote(note.stringIndex, note.fret);
        heard = true;
        editor.newNoteFret = note.fret; // keep placing notes at the fret just set
    }
    if (heard) return;
    editor.newNoteFret = std::clamp(set ? fret : editor.newNoteFret + change, 0, MAX_FRET);
    previewNote(editor.hoverString >= 0 ? editor.hoverString : 0, editor.newNoteFret);
}

// How long the selected notes are held, moved by a number of ticks
static void changeLengths(int ticks){
    for (FrettedNote& note : track().notes){
        if (!isSelected(note)) continue;
        int to = std::max(0, note.duration + ticks);
        if (to == note.duration) continue;
        note.duration = to;
        markChangedInRun();
    }
}

static int snapStep(){
    return std::max(1, editor.chart.resolution / SNAP_DIVISIONS[editor.snapIndex]);
}

static int snapTick(double tick){
    return std::max(0, (int)std::lround(tick / snapStep()) * snapStep());
}

static void copySelection(){
    if (editor.selection.empty()) return;
    clipboard.clear();
    int first = editor.selection.begin()->first;
    for (const FrettedNote& note : track().notes) if (isSelected(note)) clipboard.push_back({note.tick - first, note.stringIndex, note.fret, note.duration});
    editor.status = TextFormat("Copied %d note%s: Ctrl+V puts them at the playhead", (int)clipboard.size(), clipboard.size() == 1 ? "" : "s");
}

static void pasteAtPlayhead(){
    std::vector<FrettedNote> pasted;
    for (FrettedNote note : clipboard){
        if (note.stringIndex >= (int)track().tuning.size()) continue; // copied from a part with more strings
        note.tick += editor.playheadTick;
        pasted.push_back(note);
    }
    if (pasted.empty()) return;
    putNotes(std::vector<FrettedNote>(track().notes), pasted);
    markChanged();
}

// --- View ---------------------------------------------------------------------------------------------

static float tickToX(double tick){
    return editor.gridLeft + (float)((tick - editor.viewStartTick) / editor.chart.resolution * editor.pixelsPerBeat);
}

static double xToTick(float x){
    return editor.viewStartTick + (x - editor.gridLeft) / editor.pixelsPerBeat * editor.chart.resolution;
}

static double visibleTicks(){
    return editor.gridWidth / editor.pixelsPerBeat * editor.chart.resolution;
}

// The view stays between a little before the song's start and its end
static void clampView(){
    double lead = VIEW_LEAD * menuScale() / editor.pixelsPerBeat * editor.chart.resolution;
    editor.viewStartTick = std::clamp(editor.viewStartTick, -lead, std::max(-lead, (double)editor.chart.endTick));
}

// Zooms in or out around a point of the grid (in pixels from its left), which stays where it is
static void zoomBy(float factor, float around){
    double tick = editor.viewStartTick + around / editor.pixelsPerBeat * editor.chart.resolution;
    editor.pixelsPerBeat = std::clamp(editor.pixelsPerBeat * factor, MIN_PIXELS_PER_BEAT, MAX_PIXELS_PER_BEAT);
    editor.viewStartTick = tick - around / editor.pixelsPerBeat * editor.chart.resolution;
    clampView();
}

// Brings a tick into view if it isn't
static void showTick(int tick){
    if (tick >= editor.viewStartTick && tick <= editor.viewStartTick + visibleTicks() * 0.95) return;
    editor.viewStartTick = tick - visibleTicks() * 0.3;
    clampView();
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
    editor.scheduledNote = { editor.playheadTick - 1, INT_MAX }; // nothing before the playhead
    editor.playing = true;
}

static void stopPlayback(){
    if (!editor.playing) return;
    stopSong();
    stopPreviews(); // what was scheduled ahead mustn't play after the stop
    editor.playing = false;
}

// How long a note sounds when the part is played: for as long as it's held if it has a length, else for a moment;
// either way no further than the next note on its string, which can only play one at a time
static float soundingSeconds(const std::vector<FrettedNote>& notes, size_t index){
    const Chart& chart = editor.chart;
    const FrettedNote& note = notes[index];
    const double start = tickToSeconds(chart, note.tick);
    double end = note.duration > 0 ? tickToSeconds(chart, note.tick + note.duration) : start + RING_S;
    for (size_t i = index + 1; i < notes.size(); i++){
        double next = tickToSeconds(chart, notes[i].tick);
        if (next >= end) break;
        if (notes[i].stringIndex == note.stringIndex) end = next;
    }
    return (float)std::max(MIN_SOUND_S, end - start);
}

// Schedules what falls in the next moment: metronome clicks on each beat (the bar's first accented) and each note
// on the part's instrument, at their exact times on the engine's clock
static void schedulePlayback(){
    const Chart& chart = editor.chart;
    int horizonTick = (int)std::floor(secondsToTick(chart, playbackSeconds() + LOOKAHEAD_S)) + 1;
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
    editor.scheduledTick = std::max(editor.scheduledTick, horizonTick);

    // The notes after the last one scheduled, a few each frame. With the notes off, the place is kept up to date, so
    // turning them on again doesn't play everything that went by.
    if (!editor.playNotes){
        editor.scheduledNote = { horizonTick - 1, INT_MAX };
        return;
    }
    const std::vector<FrettedNote>& notes = track().notes;
    const bool bass = track().type == InstrumentType::Bass;
    auto it = std::upper_bound(notes.begin(), notes.end(), editor.scheduledNote, [](const NoteKey& key, const FrettedNote& note){
        return key < NoteKey(note.tick, note.stringIndex);
    });
    for (int made = 0; it != notes.end() && it->tick < horizonTick && made < NOTES_MADE_PER_FRAME; ++it, made++){
        playStringNoteAt(midiToFrequency((float)(track().tuning[it->stringIndex] + it->fret)), bass,
                         soundingSeconds(notes, it - notes.begin()), engineTime(it->tick), editor.settings->editorNoteVolume);
        editor.scheduledNote = { it->tick, it->stringIndex };
    }
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

// --- Controls -----------------------------------------------------------------------------------------
// The bars' controls are drawn by hand in the theme's colors, each over an invisible ImGui button that takes the mouse.
// Hovering one puts what it does in the bottom bar.

static ImU32 stringInk(int stringIndex, float alpha = 1.0f){
    Color color = stringColor(stringIndex);
    return IM_COL32(color.r, color.g, color.b, (int)(255.0f * alpha));
}

static float textWidth(ImFont* font, float size, const char* text){
    return font ? font->CalcTextSizeA(size, FLT_MAX, 0.0f, text).x : ImGui::CalcTextSize(text).x;
}

// A row of controls: each takes its place at `x` and moves it on, rightwards or (from the right edge) leftwards
struct Bar {
    float x;
    float middle;   // the row's middle, in y
    bool leftward;
    float s;        // the window's scale
};

static ImVec2 placeInBar(Bar& bar, float width, float height){
    ImVec2 min(bar.leftward ? bar.x - width : bar.x, bar.middle - height / 2);
    bar.x += (width + 8 * bar.s) * (bar.leftward ? -1.0f : 1.0f);
    return min;
}

static void barGap(Bar& bar, float width){
    bar.x += width * bar.s * (bar.leftward ? -1.0f : 1.0f);
}

// A thin line between two groups of controls
static void barDivider(Bar& bar){
    barGap(bar, 6);
    verticalLine(ImGui::GetWindowDrawList(), bar.x, bar.middle - 11 * bar.s, bar.middle + 11 * bar.s, 1.0f, uiColor(UiColor::StaffLine));
    barGap(bar, 14);
}

enum class Icon { None, Back, Play, Stop, Plus, Minus, Lamp, LampLit };

// A rounded button: an icon, its text ("##name" for an icon alone) and its shortcut, dim. `on` fills it with the
// accent: a choice that's active, or the thing to do next.
// `widest`: the longest text it will show, for a button whose text changes without the bar moving.
static bool barButton(Bar& bar, const char* text, const char* key, const char* tip, bool on = false, bool enabled = true,
                      Icon icon = Icon::None, const char* widest = nullptr){
    const float s = bar.s, size = 15 * s, keySize = 11.5f * s, height = BUTTON_HEIGHT * s, padding = 12 * s;
    const UiFonts& fonts = uiFonts();
    const bool hasText = text[0] != '#';
    const float iconWidth = icon == Icon::None ? 0.0f : hasText ? 16 * s : 10 * s;
    const float labelWidth = hasText ? textWidth(fonts.bold, size, widest ? widest : text) : 0.0f;
    const float keyWidth = key ? 9 * s + textWidth(fonts.mono, keySize, key) : 0.0f;
    const float width = padding + iconWidth + labelWidth + keyWidth + padding;
    const ImVec2 min = placeInBar(bar, width, height), max(min.x + width, min.y + height);

    ImGui::SetCursorScreenPos(min);
    ImGui::BeginDisabled(!enabled);
    bool pressed = ImGui::InvisibleButton(widest ? widest : text, ImVec2(width, height));
    bool hovered = ImGui::IsItemHovered(), held = ImGui::IsItemActive();
    ImGui::EndDisabled();
    if (hovered){
        hoverTip = tip;
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    }

    ImDrawList* draw = ImGui::GetWindowDrawList();
    const float alpha = enabled ? 1.0f : 0.38f;
    if (on){
        draw->AddRectFilled(min, max, uiColor(UiColor::Accent, held ? 0.8f : 1.0f), height / 2);
        if (hovered) draw->AddRectFilled(min, max, uiColor(UiColor::Card, 0.12f), height / 2);
    } else {
        draw->AddRectFilled(min, max, uiColor(UiColor::Card, alpha), height / 2);
        if (hovered) draw->AddRectFilled(min, max, uiColor(UiColor::Ink, held ? 0.12f : 0.06f), height / 2);
        draw->AddRect(min, max, uiColor(hovered ? UiColor::Dim : UiColor::StaffLine, alpha), height / 2, 0, std::max(1.0f, s));
    }
    const ImU32 ink = on ? uiColor(UiColor::Card) : uiColor(UiColor::Ink, alpha * (hovered ? 1.0f : 0.86f));
    float x = min.x + padding;
    const float y = bar.middle, half = 5 * s;
    switch (icon){
        case Icon::Back: {
            ImVec2 points[3] = { ImVec2(x + half, y - half), ImVec2(x, y), ImVec2(x + half, y + half) };
            draw->AddPolyline(points, 3, ink, ImDrawFlags_None, std::max(1.5f, 1.8f * s));
            break;
        }
        case Icon::Play: draw->AddTriangleFilled(ImVec2(x, y - half - s), ImVec2(x + 2 * half, y), ImVec2(x, y + half + s), ink); break;
        case Icon::Stop: draw->AddRectFilled(ImVec2(x, y - half), ImVec2(x + 2 * half, y + half), ink, 2 * s); break;
        case Icon::Plus:
            verticalLine(draw, x + half, y - half, y + half, std::max(1.5f, 2 * s), ink);
            [[fallthrough]]; // then its bar, as a minus
        case Icon::Minus: horizontalLine(draw, x, x + 2 * half, y, std::max(1.5f, 2 * s), ink); break;
        case Icon::Lamp: draw->AddCircle(ImVec2(x + 4 * s, y), 3.5f * s, uiColor(UiColor::Dim, alpha), 0, 1.5f * s); break;
        case Icon::LampLit: draw->AddCircleFilled(ImVec2(x + 4 * s, y), 4.5f * s, uiColor(UiColor::Accent, alpha)); break;
        case Icon::None: break;
    }
    x += iconWidth;
    if (hasText) draw->AddText(fonts.bold, size, ImVec2(x, y - size / 2 - s), ink, text);
    if (key) draw->AddText(fonts.mono, keySize, ImVec2(x + labelWidth + 9 * s, y - keySize / 2), on ? uiColor(UiColor::Card, 0.75f) : uiColor(UiColor::Dim, alpha), key);
    return pressed;
}

// A switch: the same button with a lamp, lit while it's on
static bool barToggle(Bar& bar, const char* text, const char* tip, bool on, bool enabled = true){
    return barButton(bar, text, nullptr, tip, false, enabled, on ? Icon::LampLit : Icon::Lamp);
}

// A slim slider from 0 to 1 with its name before it
static bool barSlider(Bar& bar, const char* label, float* value, const char* tip){
    const float s = bar.s, labelSize = 12 * s, track = 84 * s, knob = 7 * s, height = BUTTON_HEIGHT * s;
    const UiFonts& fonts = uiFonts();
    const float labelWidth = textWidth(fonts.mono, labelSize, label);
    const float width = labelWidth + 10 * s + knob + track + knob;
    const ImVec2 min = placeInBar(bar, width, height);
    const float trackLeft = min.x + labelWidth + 10 * s + knob, trackRight = trackLeft + track;

    ImGui::SetCursorScreenPos(min);
    ImGui::PushID("slider"); // a button may have the same name (the Notes switch)
    ImGui::InvisibleButton(label, ImVec2(width, height));
    ImGui::PopID();
    bool hovered = ImGui::IsItemHovered(), held = ImGui::IsItemActive(), changed = false;
    if (hovered) hoverTip = tip;
    if (held){
        float to = std::clamp((ImGui::GetIO().MousePos.x - trackLeft) / track, 0.0f, 1.0f);
        changed = to != *value;
        *value = to;
    }

    ImDrawList* draw = ImGui::GetWindowDrawList();
    const float y = bar.middle, knobX = trackLeft + *value * track;
    draw->AddText(fonts.mono, labelSize, ImVec2(min.x, y - labelSize / 2 - s), uiColor(hovered ? UiColor::Ink : UiColor::Dim), label);
    draw->AddRectFilled(ImVec2(trackLeft, y - 2 * s), ImVec2(trackRight, y + 2 * s), uiColor(UiColor::Dim, 0.3f), 2 * s);
    draw->AddRectFilled(ImVec2(trackLeft, y - 2 * s), ImVec2(knobX, y + 2 * s), uiColor(UiColor::Accent), 2 * s);
    draw->AddCircleFilled(ImVec2(knobX, y), knob * (held ? 1.15f : 1.0f), uiColor(UiColor::Card));
    draw->AddCircle(ImVec2(knobX, y), knob * (held ? 1.15f : 1.0f), uiColor(UiColor::Accent), 0, 2 * s);
    return changed;
}

// ImGui's own widgets (fields, popups) at the editor's size: the menus' are bigger, for reading from a distance
static void pushCompactStyle(float s){
    ImGui::PushFont(uiFonts().text, 15 * s);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10 * s, 6 * s));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8 * s, 6 * s));
}

static void popCompactStyle(){
    ImGui::PopStyleVar(2);
    ImGui::PopFont();
}

// "12.3": the bar and the beat a tick falls in, counted from 1
static const char* positionText(int tick){
    const Chart& chart = editor.chart;
    int bar = barNumberAt(chart, tick);
    int beat = chart.resolution * 4 / timeSignatureAt(chart, tick).beatUnit;
    return TextFormat("%d.%d", bar + 1, (tick - barStartTick(chart, bar)) / beat + 1);
}

// "G1": a pitch's name
static const char* pitchText(int pitch){
    return TextFormat("%s%d", pitchClassName(pitch), pitchOctave(pitch));
}

// --- Bars ---------------------------------------------------------------------------------------------

static void drawTopBar(float s, float width, bool& requestBack, bool& requestTestPlay){
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const UiFonts& fonts = uiFonts();

    Bar right{ width - 16 * s, TOP_BAR * s / 2, true, s };
    if (barButton(right, "Save", "Ctrl+S", editor.builtIn ? "Save: a built-in song is saved as your own copy" : "Save the song", editor.dirty)) saveEditorChart();
    if (barButton(right, "Share", nullptr, "Make the saved song one file anyone can install, in your packages folder")) shareSong();
    if (barButton(right, "Test play", "F5", "Play it for real, from the playhead")) requestTestPlay = true;
    barGap(right, 8);
    if (barToggle(right, "Details", "The song's title, parts, tempo, offset, length, time signature and key", editor.drawerOpen)){
        editor.drawerOpen = !editor.drawerOpen;
    }
    if (barButton(right, "Keys", "F1", "Everything the mouse and the keyboard do here")) editor.openKeys = true;
    barGap(right, 8);
    if (barButton(right, "Redo", nullptr, "Redo (Ctrl+Y)", false, !editor.redoStack.empty())) stepHistory(editor.redoStack, editor.undoStack);
    if (barButton(right, "Undo", nullptr, "Undo (Ctrl+Z)", false, !editor.undoStack.empty() || editor.uncommitted)) stepHistory(editor.undoStack, editor.redoStack);

    Bar left{ 16 * s, TOP_BAR * s / 2, false, s };
    if (barButton(left, "Back", "Esc", "Back to your songs", false, true, Icon::Back)) requestBack = true;

    // The song, with a dot while it has unsaved changes; under it, what last happened, or else who it's by
    const float x = left.x + 8 * s, titleSize = 19 * s, lineSize = 12.5f * s;
    const ImVec4 clip(x, 0, right.x - 12 * s, TOP_BAR * s);
    const std::string& line = editor.status.empty() ? editor.chart.artist : editor.status;
    const float titleY = line.empty() ? (TOP_BAR * s - titleSize) / 2 - s : 9 * s;
    draw->AddText(fonts.bold, titleSize, ImVec2(x, titleY), uiColor(UiColor::Ink), editor.chart.title.c_str(), nullptr, 0.0f, &clip);
    if (editor.dirty){
        float dotX = x + textWidth(fonts.bold, titleSize, editor.chart.title.c_str()) + 10 * s;
        if (dotX < clip.z) draw->AddCircleFilled(ImVec2(dotX, titleY + titleSize * 0.56f), 3.5f * s, uiColor(UiColor::Accent));
    }
    if (!line.empty()) draw->AddText(fonts.mono, lineSize, ImVec2(x, 34 * s), uiColor(UiColor::Dim), line.c_str(), nullptr, 0.0f, &clip);
}

static void drawToolBar(float s, float width){
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const UiFonts& fonts = uiFonts();
    const Chart& chart = editor.chart;
    const float top = TOP_BAR * s, bottom = top + TOOL_BAR * s;
    horizontalLine(draw, 0, width, bottom - 1.0f, 1.0f, uiColor(UiColor::StaffLine));

    Bar bar{ 16 * s, top + TOOL_BAR * s / 2 - 3 * s, false, s };
    if (barButton(bar, editor.playing ? "Stop" : "Play", "Space", editor.songLoaded ? "Play from the playhead" : "Play from the playhead: with no audio, the clicks and the notes alone",
                  editor.playing, true, editor.playing ? Icon::Stop : Icon::Play, "Stop")){
        if (editor.playing) stopPlayback();
        else startPlayback();
    }

    // Where the playhead is: bar and beat, then the time in the song
    int playhead = editor.playing ? std::max(0, (int)secondsToTick(chart, playbackSeconds())) : editor.playheadTick;
    double seconds = std::max(0.0, tickToSeconds(chart, playhead));
    const float readout = 15 * s;
    barGap(bar, 6);
    draw->AddText(fonts.mono, readout, ImVec2(bar.x, bar.middle - readout / 2 - s), uiColor(UiColor::Ink), positionText(playhead));
    draw->AddText(fonts.mono, readout, ImVec2(bar.x + 62 * s, bar.middle - readout / 2 - s), uiColor(UiColor::Dim),
                  TextFormat("%d:%05.2f", (int)seconds / 60, std::fmod(seconds, 60.0)));
    barGap(bar, 138);
    barDivider(bar);

    // The song's parts: the one being edited, and adding one (taking one away is in Details)
    for (int i = 0; i < (int)chart.frettedTracks.size(); i++){
        const FrettedTrack& part = chart.frettedTracks[i];
        ImGui::PushID(i);
        if (barButton(bar, part.name.empty() ? "Part" : part.name.c_str(), nullptr,
                      part.type == InstrumentType::Bass ? "Edit this bass part" : "Edit this guitar part", i == editor.part)) choosePart(i);
        ImGui::PopID();
    }
    const float addX = bar.x;
    if (barButton(bar, "##addpart", nullptr, "Add a guitar or a bass part", false, true, Icon::Plus)) ImGui::OpenPopup(ADD_PART_POPUP);
    ImGui::SetNextWindowPos(ImVec2(addX, bar.middle + BUTTON_HEIGHT * s / 2 + 4 * s));
    pushCompactStyle(s);
    if (ImGui::BeginPopup(ADD_PART_POPUP)){
        if (ImGui::Selectable("Guitar part")) addPart(InstrumentType::Guitar);
        if (ImGui::Selectable("Bass part")) addPart(InstrumentType::Bass);
        ImGui::EndPopup();
    }
    popCompactStyle();
    barDivider(bar);

    // The grid notes snap to
    const float labelSize = 12 * s, dropdownWidth = 150 * s;
    draw->AddText(fonts.mono, labelSize, ImVec2(bar.x, bar.middle - labelSize / 2 - s), uiColor(UiColor::Dim), "Grid");
    barGap(bar, 40);
    const ImVec2 dropdown = placeInBar(bar, dropdownWidth, BUTTON_HEIGHT * s);
    settingsDropdownAt("grid", dropdown, ImVec2(dropdown.x + dropdownWidth, dropdown.y + BUTTON_HEIGHT * s), &editor.snapIndex, SNAP_LABELS);
    if (ImGui::IsItemHovered()) hoverTip = "The grid notes snap to: how finely a beat is cut";
    barDivider(bar);

    // What's heard while it plays, and whether the song's waveform is shown
    if (barToggle(bar, "Click", "A metronome click on each beat while it plays", editor.metronome)) editor.metronome = !editor.metronome;
    if (barToggle(bar, "Notes", "The part's notes played over the song while it plays", editor.playNotes)) editor.playNotes = !editor.playNotes;
    if (barToggle(bar, "Waveform",
                  !editor.songLoaded ? "The song's waveform: this song has no audio"
                  : waveform.show && !waveform.ready ? "The song's waveform: reading the audio..."
                  : "The song's waveform over the strings: with the offset and the tempo right, its attacks sit on the beats",
                  waveform.show && editor.songLoaded, editor.songLoaded)) waveform.show = !waveform.show;
}

// A line of hints, "Key  what it does      Key  what it does": six spaces between hints, two after a hint's key. The
// keys are drawn in ink, the rest dim; a line with no key in it (what a button does) is all dim.
static void drawHints(ImDrawList* draw, const char* text, ImVec2 at, float size, const ImVec4& clip){
    const UiFonts& fonts = uiFonts();
    std::string line = text;
    float x = at.x;
    for (size_t from = 0; from < line.size(); ){
        size_t end = line.find("      ", from);
        if (end == std::string::npos) end = line.size();
        std::string hint = line.substr(from, end - from);
        size_t split = hint.find("  ");
        if (split != std::string::npos){
            std::string key = hint.substr(0, split);
            draw->AddText(fonts.bold, size, ImVec2(x, at.y), uiColor(UiColor::Ink, 0.85f), key.c_str(), nullptr, 0.0f, &clip);
            x += textWidth(fonts.bold, size, key.c_str()) + 7 * size / 14;
            hint = hint.substr(split + 2);
        }
        draw->AddText(fonts.text, size, ImVec2(x, at.y), uiColor(UiColor::Dim), hint.c_str(), nullptr, 0.0f, &clip);
        x += textWidth(fonts.text, size, hint.c_str()) + 22 * size / 14;
        from = end + 6;
    }
}

// The bottom bar: what the thing under the mouse does, the editor's two volumes, the zoom
static void drawBottomBar(float s, float width, float height, const char* hint){
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const float top = height - BOTTOM_BAR * s;
    horizontalLine(draw, 0, width, top, 1.0f, uiColor(UiColor::StaffLine));

    Bar right{ width - 16 * s, top + BOTTOM_BAR * s / 2, true, s };
    if (barButton(right, "##zoomin", nullptr, "Zoom in (Ctrl + wheel)", false, true, Icon::Plus)) zoomBy(1.3f, editor.gridWidth / 2);
    if (barButton(right, "##zoomout", nullptr, "Zoom out (Ctrl + wheel)", false, true, Icon::Minus)) zoomBy(1.0f / 1.3f, editor.gridWidth / 2);
    barGap(right, 10);
    Settings& settings = *editor.settings;
    barSlider(right, "Notes", &settings.editorNoteVolume, "How loud the notes are: the ones you place, and the part played over the song, on its own instrument");
    barGap(right, 8);
    if (barSlider(right, "Song", &settings.editorSongVolume, "How loud the song is under the notes, here in the editor")) setSongVolume(settings.editorSongVolume);

    const float size = 14 * s;
    const ImVec4 clip(16 * s, top, right.x - 12 * s, height);
    drawHints(draw, hoverTip.empty() ? hint : hoverTip.c_str(), ImVec2(16 * s, top + (BOTTOM_BAR * s - size) / 2 - s), size, clip);
}

// --- Details ------------------------------------------------------------------------------------------
// The song's details, in a drawer beside the timeline: ImGui's own fields, in the theme's style

static void drawDetails(ImVec2 min, ImVec2 max, float s){
    Chart& chart = editor.chart;
    ImGui::SetCursorScreenPos(min);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, uiColorVec(UiColor::Card));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(18 * s, 4 * s));
    pushCompactStyle(s);
    ImGui::BeginChild("Details", ImVec2(max.x - min.x, max.y - min.y), ImGuiChildFlags_AlwaysUseWindowPadding);
    verticalLine(ImGui::GetWindowDrawList(), min.x, min.y, max.y, 1.0f, uiColor(UiColor::StaffLine));

    auto field = [&](const char* label){
        ImGui::Dummy(ImVec2(0, 2 * s));
        ImGui::PushFont(uiFonts().text, 13.5f * s);
        ImGui::TextColored(uiColorVec(UiColor::Dim), "%s", label);
        ImGui::PopFont();
        ImGui::SetNextItemWidth(-FLT_MIN);
    };
    auto note = [&](const char* text){
        ImGui::PushFont(uiFonts().text, 13.5f * s);
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextColored(uiColorVec(UiColor::Dim), "%s", text);
        ImGui::PopTextWrapPos();
        ImGui::PopFont();
    };

    settingsGroup("SONG");
    field("Title");
    if (ImGui::InputText("##title", &chart.title)) markChanged();
    field("Artist");
    if (ImGui::InputText("##artist", &chart.artist)) markChanged();
    note(chart.audioFile.empty() ? "No audio: it plays as clicks and notes alone" : TextFormat("Audio: %s", chart.audioFile.c_str()));

    settingsGroup("THIS PART");
    field("Name");
    if (ImGui::InputText("##part", &track().name)) markChanged();
    std::string tuning;
    for (int pitch : track().tuning) tuning += TextFormat("%s%s", tuning.empty() ? "" : " ", pitchText(pitch));
    note(TextFormat("%s, tuned %s, %d notes", track().type == InstrumentType::Bass ? "Bass" : "Guitar", tuning.c_str(), (int)track().notes.size()));
    ImGui::BeginDisabled(chart.frettedTracks.size() < 2); // a song keeps at least one part
    if (ImGui::Button("Remove this part")) removePart();
    ImGui::EndDisabled();

    settingsGroup("TIMING");
    field("Tempo (BPM)");
    double bpm = chart.tempoMap[0].bpm;
    if (ImGui::InputDouble("##bpm", &bpm, 1.0, 10.0, "%.3f") && bpm >= 1.0 && bpm <= 1000.0){
        chart.tempoMap[0].bpm = bpm;
        markChanged();
    }
    if (chart.tempoMap.size() > 1) note(TextFormat("+ %d tempo changes", (int)chart.tempoMap.size() - 1));
    field("Offset: where the first bar starts in the audio (s)");
    if (ImGui::InputDouble("##offset", &chart.offset, 0.001, 0.01, "%.3f")) markChanged();
    field("Length (bars)");
    int bars = barNumberAt(chart, chart.endTick - 1) + 1;
    if (ImGui::InputInt("##bars", &bars)){
        int lastNoteTick = 0; // of any part
        for (const FrettedTrack& part : chart.frettedTracks) if (!part.notes.empty()) lastNoteTick = std::max(lastNoteTick, part.notes.back().tick);
        int barsNeeded = barNumberAt(chart, lastNoteTick) + 1; // never shorter than the last note's bar
        chart.endTick = barStartTick(chart, std::max(std::max(bars, 1), barsNeeded));
        markChanged();
    }

    // The song's time signature and key. Later changes must stay on bar lines, so with any, these are read-only
    // here (edit them in the file); like the tempo, the number of changes is shown.
    settingsGroup("NOTATION");
    TimeSignatureChange& time = chart.timeSignatures[0];
    ImGui::BeginDisabled(chart.timeSignatures.size() > 1);
    field("Beats in a bar");
    int beats = time.beats;
    if (ImGui::InputInt("##beats", &beats) && beats >= 1 && beats <= 32){
        time.beats = beats;
        markChanged();
    }
    field("A beat is a");
    if (ImGui::BeginCombo("##unit", TextFormat("1/%d", time.beatUnit))){
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
    if (chart.timeSignatures.size() > 1) note(TextFormat("+ %d time signature changes", (int)chart.timeSignatures.size() - 1));

    KeySignature& key = chart.keys[0].key;
    ImGui::BeginDisabled(chart.keys.size() > 1);
    field("Key");
    if (ImGui::BeginCombo("##key", keySignatureName(key).c_str())){
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
    if (chart.keys.size() > 1) note(TextFormat("+ %d key changes", (int)chart.keys.size() - 1));
    ImGui::Dummy(ImVec2(0, 16 * s));

    ImGui::EndChild();
    popCompactStyle();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
}

// --- Timeline -----------------------------------------------------------------------------------------
// A custom ImGui widget: an invisible button reserves the area and captures the mouse, and everything visible is drawn
// by hand with the window's draw list. From the top: the ruler (bars and beats), the song's waveform in a lane of its
// own, then a row per string.

// The time in the song at a tick, before its start too (tickToSeconds stops there): the audio that comes before the
// first bar is drawn as well
static double songSeconds(double tick){
    const Chart& chart = editor.chart;
    if (tick >= 0.0) return tickToSeconds(chart, (int)tick);
    return chart.offset + tick / chart.resolution * 60.0 / chart.tempoMap[0].bpm;
}

// The waveform between two heights, over the pixels from `left` to `right`; `tickAt` gives each pixel's place in the song
template <typename TickAt>
static void drawWaveform(ImDrawList* draw, float left, float right, float top, float bottom, ImU32 color, TickAt tickAt){
    if (!waveform.ready || waveform.peaks.empty()) return;
    const std::vector<float>& peaks = waveform.peaks;
    const float middle = (top + bottom) / 2, half = (bottom - top) / 2, column = 2.0f;
    for (float x = left; x < right; x += column){
        int first = (int)(songSeconds(tickAt(x)) * PEAKS_PER_SECOND), last = std::max(first, (int)(songSeconds(tickAt(x + column)) * PEAKS_PER_SECOND) - 1);
        if (last < 0 || first >= (int)peaks.size()) continue;
        float peak = 0.0f;
        for (int i = std::max(first, 0); i <= last && i < (int)peaks.size(); i++) peak = std::max(peak, peaks[i]);
        draw->AddRectFilled(ImVec2(x, middle - peak * half), ImVec2(x + column - 0.5f, middle + std::max(peak * half, 0.5f)), color);
    }
}

static void finishDrag(){
    switch (editor.drag){
        case Drag::Note: if (!ImGui::GetIO().KeyCtrl) editor.selection = { editor.dragNote }; break; // a click: that note alone
        case Drag::Move: if (editor.dragChanged) markChanged(); break;
        case Drag::Playhead: if (editor.resumeAfterDrag) startPlayback(); break;
        default: break;
    }
    editor.drag = Drag::None;
    editor.dragBase.clear();
}

static void drawTimeline(ImVec2 min, ImVec2 max, float s){
    ImGuiIO& io = ImGui::GetIO();
    const Chart& chart = editor.chart;
    const UiFonts& fonts = uiFonts();
    const int resolution = chart.resolution;
    const int strings = (int)track().tuning.size();
    const bool lowOnTop = editor.settings->lowStringOnTop;

    ImGui::SetCursorScreenPos(min);
    ImGui::InvisibleButton("timeline", ImVec2(max.x - min.x, max.y - min.y),
                           ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);
    const bool hovered = ImGui::IsItemHovered(), held = ImGui::IsItemActive();

    // Where things are: the rows take what the waveform's lane leaves, up to a comfortable height
    const bool wave = waveform.show && editor.songLoaded;
    const float gridLeft = min.x + LABEL_WIDTH * s, gridRight = max.x;
    const float rulerBottom = min.y + RULER_HEIGHT * s, room = max.y - rulerBottom;
    const float rowHeight = std::min((wave ? MAX_ROW_HEIGHT : MAX_ROW_HEIGHT_ALONE) * s, (room - (wave ? MIN_WAVE_HEIGHT * s : 0.0f)) / strings);
    const float waveHeight = wave ? std::clamp(room - rowHeight * strings, MIN_WAVE_HEIGHT * s, MAX_WAVE_HEIGHT * s) : 0.0f;
    const float rowsTop = rulerBottom + waveHeight, rowsBottom = rowsTop + rowHeight * strings;
    const float radius = std::min(NOTE_RADIUS * s, rowHeight * 0.4f);
    editor.gridLeft = gridLeft;
    editor.gridWidth = std::max(1.0f, gridRight - gridLeft);
    clampView();
    // String <-> screen row, following the string order setting (the mapping is its own inverse)
    auto stringToRow = [&](int stringIndex){ return lowOnTop ? stringIndex : strings - 1 - stringIndex; };
    auto rowY = [&](int stringIndex){ return rowsTop + (stringToRow(stringIndex) + 0.5f) * rowHeight; };
    // Where a note's length is taken hold of: the end of its tail, or just past the note when it has none
    auto gripX = [&](const FrettedNote& note){ return std::max(tickToX(note.tick + note.duration), tickToX(note.tick) + radius + 7 * s); };

    // The playhead: where playback starts, or where it is while playing. Playing, the view follows it.
    int playhead = editor.playing ? (int)secondsToTick(chart, playbackSeconds()) : editor.playheadTick;
    if (editor.playing && editor.drag != Drag::Pan){
        if (playhead > editor.viewStartTick + visibleTicks() * FOLLOW_AT || playhead < editor.viewStartTick){
            editor.viewStartTick = playhead - visibleTicks() * FOLLOW_AT;
            clampView();
        }
    }

    // --- The mouse ---
    const ImVec2 mouse = io.MousePos;
    const int mouseRow = (int)std::floor((mouse.y - rowsTop) / rowHeight);
    const bool overRows = hovered && mouse.x >= gridLeft && mouseRow >= 0 && mouseRow < strings;
    const bool overTop = hovered && mouse.x >= gridLeft && mouse.y < rowsTop; // the ruler and the waveform
    editor.hoverString = overRows ? stringToRow(mouseRow) : -1;

    // The note under the mouse: the nearest one whose circle it's in, or else one whose grip it's on. A note with no
    // tail offers its grip only once it's selected, or the grips would take the clicks meant for the next grid line.
    NoteKey hoverKey;
    bool overNote = false, overGrip = false;
    if (hovered && mouse.x >= gridLeft && (editor.drag == Drag::None || editor.drag == Drag::Erase)){
        const double margin = (radius + 16 * s) / editor.pixelsPerBeat * resolution;
        const double from = editor.viewStartTick - margin, to = editor.viewStartTick + visibleTicks() + margin;
        float nearest = radius * radius;
        for (const FrettedNote& note : track().notes){
            if (note.tick > to) break;
            if (note.tick < from) continue;
            float dx = mouse.x - tickToX(note.tick), dy = mouse.y - rowY(note.stringIndex);
            if (dx * dx + dy * dy > nearest) continue;
            nearest = dx * dx + dy * dy;
            hoverKey = { note.tick, note.stringIndex };
            overNote = true;
        }
        if (!overNote) for (const FrettedNote& note : track().notes){
            if (note.tick > to) break;
            if (note.duration == 0 && !(editor.selection.size() == 1 && isSelected(note))) continue;
            if (std::fabs(mouse.x - gripX(note)) > 7 * s || std::fabs(mouse.y - rowY(note.stringIndex)) > radius) continue;
            hoverKey = { note.tick, note.stringIndex };
            overGrip = true;
        }
    }

    if (ImGui::IsItemClicked(ImGuiMouseButton_Left)){
        editor.dragFrom = mouse;
        editor.dragFromTick = xToTick(mouse.x);
        editor.dragMoved = editor.dragChanged = false;
        if (overTop){
            editor.drag = Drag::Playhead;
            editor.resumeAfterDrag = editor.playing;
            stopPlayback();
        } else if (overGrip){
            editor.drag = Drag::Length;
            editor.dragNote = hoverKey;
            if (!editor.selection.count(hoverKey)) editor.selection = { hoverKey };
        } else if (overNote){
            if (io.KeyCtrl){ // Ctrl + click: in or out of the selection
                if (!editor.selection.erase(hoverKey)) editor.selection.insert(hoverKey);
            } else {
                if (!editor.selection.count(hoverKey)) editor.selection = { hoverKey };
                editor.drag = Drag::Note;
                editor.dragNote = hoverKey;
            }
            const FrettedNote& note = *findNote(hoverKey.first, hoverKey.second);
            previewNote(note.stringIndex, note.fret);
            editor.newNoteFret = note.fret; // and it's the fret of the next note placed
        } else if (overRows && io.KeyShift){
            editor.drag = Drag::Box;
            if (!io.KeyCtrl) editor.selection.clear();
            editor.dragSelection = editor.selection;
        } else if (overRows){
            // A new note, heard; still held, dragging right gives it a length
            int tick = snapTick(xToTick(mouse.x));
            addNote(tick, editor.hoverString, editor.newNoteFret);
            previewNote(editor.hoverString, editor.newNoteFret);
            editor.drag = Drag::Length;
            editor.dragNote = { tick, editor.hoverString };
        }
    }
    if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) editor.drag = Drag::Erase;
    if (ImGui::IsItemClicked(ImGuiMouseButton_Middle)){
        editor.drag = Drag::Pan;
        editor.dragFrom = mouse;
        editor.panFromTick = editor.viewStartTick;
    }

    if (editor.drag != Drag::None && !held) finishDrag();
    if (editor.drag != Drag::None){
        if (std::fabs(mouse.x - editor.dragFrom.x) > DRAG_STARTS * s || std::fabs(mouse.y - editor.dragFrom.y) > DRAG_STARTS * s) editor.dragMoved = true;
        // Held against an edge, the view scrolls under the drag
        if (editor.drag != Drag::Pan && editor.drag != Drag::Erase && editor.dragMoved){
            float past = mouse.x > gridRight - 20 * s ? 1.0f : mouse.x < gridLeft + 20 * s ? -1.0f : 0.0f;
            editor.viewStartTick += past * 700.0f * s * io.DeltaTime / editor.pixelsPerBeat * resolution;
            clampView();
        }
        const double mouseTick = xToTick(mouse.x);
        switch (editor.drag){
            case Drag::Playhead:
                editor.playheadTick = std::min(snapTick(mouseTick), chart.endTick);
                break;
            case Drag::Note:
                if (!editor.dragMoved) break;
                editor.drag = Drag::Move;
                editor.dragBase = track().notes;
                editor.dragSelection = editor.selection;
                editor.dragEndTick = chart.endTick;
                [[fallthrough]];
            case Drag::Move: {
                int ticks = (int)std::lround((mouseTick - editor.dragFromTick) / snapStep()) * snapStep();
                int rows = mouseRow - (int)std::floor((editor.dragFrom.y - rowsTop) / rowHeight);
                editor.chart.endTick = editor.dragEndTick;
                editor.dragChanged = moveNotes(editor.dragBase, editor.dragSelection, ticks, lowOnTop ? rows : -rows);
                break;
            }
            case Drag::Length: {
                auto grabbed = findNote(editor.dragNote.first, editor.dragNote.second);
                if (!editor.dragMoved || grabbed == track().notes.end()) break;
                int length = std::max(0, snapTick(mouseTick) - grabbed->tick);
                bool all = isSelected(*grabbed); // the other selected notes take the same length
                for (FrettedNote& note : track().notes){
                    if (&note != &*grabbed && !(all && isSelected(note))) continue;
                    if (note.duration == length) continue;
                    note.duration = length;
                    markChanged();
                }
                break;
            }
            case Drag::Box: {
                editor.selection = editor.dragSelection;
                float x0 = tickToX(editor.dragFromTick), x1 = mouse.x, y0 = editor.dragFrom.y, y1 = mouse.y;
                if (x0 > x1) std::swap(x0, x1);
                if (y0 > y1) std::swap(y0, y1);
                for (const FrettedNote& note : track().notes){
                    float x = tickToX(note.tick), y = rowY(note.stringIndex);
                    if (x >= x0 && x <= x1 && y >= y0 && y <= y1) editor.selection.insert({note.tick, note.stringIndex});
                }
                break;
            }
            case Drag::Erase:
                if (overNote) deleteNote(hoverKey.first, hoverKey.second);
                overNote = false;
                break;
            case Drag::Pan:
                editor.viewStartTick = editor.panFromTick - (mouse.x - editor.dragFrom.x) / editor.pixelsPerBeat * resolution;
                clampView();
                break;
            case Drag::None: break;
        }
    }

    // The wheel. On a string: the fret, of the note under the mouse (with the rest of the selection if it's part of
    // one) or of the next note placed. With Shift, or over the ruler and the waveform: along the song. With Ctrl: the
    // zoom, keeping the point under the mouse still.
    if (hovered && (io.MouseWheel != 0.0f || io.MouseWheelH != 0.0f)){
        float along = io.MouseWheelH;
        if (io.KeyCtrl) zoomBy(std::pow(1.15f, io.MouseWheel), mouse.x - gridLeft);
        else if (io.KeyShift || !overRows) along += io.MouseWheel;
        else if (editor.drag == Drag::None){
            editor.wheelCarry += io.MouseWheel;
            int notches = (int)editor.wheelCarry; // whole ones: a trackpad sends fractions
            editor.wheelCarry -= notches;
            if (notches != 0){
                if (overNote && !editor.selection.count(hoverKey)) editor.selection = { hoverKey };
                else if (!overNote) editor.selection.clear();
                changeFrets(notches, false, 0);
            }
        }
        if (along != 0.0f){
            editor.viewStartTick -= along * WHEEL_SCROLL * s / editor.pixelsPerBeat * resolution;
            clampView();
        }
    }

    // --- Drawing ---
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const double viewEndTick = xToTick(gridRight);
    draw->AddRectFilled(min, ImVec2(max.x, rowsBottom), uiColor(UiColor::Card));
    draw->PushClipRect(ImVec2(gridLeft, min.y), ImVec2(gridRight, rowsBottom), true);

    // Every other bar a shade darker, and each bar's number on the ruler
    for (int bar = barNumberAt(chart, std::max(0, (int)editor.viewStartTick)); barStartTick(chart, bar) <= viewEndTick; bar++){
        float x = tickToX(barStartTick(chart, bar));
        if (bar % 2 == 1) draw->AddRectFilled(ImVec2(x, rulerBottom), ImVec2(tickToX(barStartTick(chart, bar + 1)), rowsBottom), uiColor(UiColor::Ink, 0.035f));
        draw->AddText(fonts.mono, 12 * s, ImVec2(x + 6 * s, min.y + 6 * s), uiColor(UiColor::Dim), TextFormat("%d", bar + 1));
    }

    // The song's waveform, placed by the chart's timing: when the offset and tempo are right, its attacks line up
    // with the beats
    if (wave){
        if (waveform.ready) drawWaveform(draw, gridLeft, gridRight, rulerBottom + 5 * s, rowsTop - 5 * s, uiColor(UiColor::Ink, 0.42f), xToTick);
        else draw->AddText(fonts.mono, 12 * s, ImVec2(gridLeft + 12 * s, rulerBottom + waveHeight / 2 - 7 * s), uiColor(UiColor::Dim), "reading the audio...");
    }

    // Grid: bar lines strongest, then beats, then the snap subdivisions (hidden when too close together)
    int step = snapStep();
    if (editor.pixelsPerBeat / SNAP_DIVISIONS[editor.snapIndex] < MIN_GRID_LINE_SPACING) step = resolution;
    for (int tick = std::max(0, (int)(editor.viewStartTick / step) * step); tick <= viewEndTick; tick += step){
        float x = tickToX(tick);
        int barStart = barStartTick(chart, barNumberAt(chart, tick));
        int beatLength = resolution * 4 / timeSignatureAt(chart, tick).beatUnit; // a beat of 1/8 is half a quarter
        if (tick == barStart) verticalLine(draw, x, min.y + 4 * s, rowsBottom, std::max(1.0f, 1.5f * s), uiColor(UiColor::Ink, 0.34f));
        else if ((tick - barStart) % beatLength == 0) verticalLine(draw, x, rulerBottom - 6 * s, rowsBottom, 1.0f, uiColor(UiColor::Ink, 0.15f));
        else verticalLine(draw, x, rowsTop, rowsBottom, 1.0f, uiColor(UiColor::Ink, 0.065f));
    }
    horizontalLine(draw, gridLeft, gridRight, rulerBottom, 1.0f, uiColor(UiColor::StaffLine));
    if (wave) horizontalLine(draw, gridLeft, gridRight, rowsTop, 1.0f, uiColor(UiColor::StaffLine));

    // The strings, the low ones thicker; the one under the mouse a touch brighter
    for (int i = 0; i < strings; i++){
        if (i == editor.hoverString && editor.drag == Drag::None){
            float top = rowsTop + stringToRow(i) * rowHeight;
            draw->AddRectFilled(ImVec2(gridLeft, top), ImVec2(gridRight, top + rowHeight), uiColor(UiColor::Ink, 0.03f));
        }
        float weight = strings > 1 ? 1.0f - (float)i / (strings - 1) : 0.0f;
        horizontalLine(draw, gridLeft, gridRight, rowY(i), (1.0f + 1.3f * weight) * s, uiColor(UiColor::Ink, 0.24f));
    }

    // Before the song's start and after its end: faded out
    float startX = tickToX(0), endX = tickToX(chart.endTick);
    if (startX > gridLeft) draw->AddRectFilled(ImVec2(gridLeft, rulerBottom + 1.0f), ImVec2(startX, rowsBottom), uiColor(UiColor::Background, 0.6f));
    if (endX < gridRight){
        draw->AddRectFilled(ImVec2(std::max(endX, gridLeft), rulerBottom + 1.0f), ImVec2(gridRight, rowsBottom), uiColor(UiColor::Background, 0.6f));
        verticalLine(draw, endX, min.y + 4 * s, rowsBottom, 2.0f * s, uiColor(UiColor::Dim));
        draw->AddText(fonts.mono, 12 * s, ImVec2(endX + 6 * s, min.y + 6 * s), uiColor(UiColor::Dim), "end");
    }

    // Where a new note would go: its place on the grid marked up through the waveform, the note itself faint, with
    // its fret and its name
    const bool ghost = overRows && !overNote && !overGrip && editor.drag == Drag::None && !io.KeyShift;
    if (ghost){
        int tick = snapTick(xToTick(mouse.x)), pitch = track().tuning[editor.hoverString] + editor.newNoteFret;
        ImVec2 center(tickToX(tick), rowY(editor.hoverString));
        verticalLine(draw, center.x, rulerBottom, rowsBottom, 1.0f, uiColor(UiColor::Accent, 0.55f));
        draw->AddCircleFilled(center, radius, stringInk(editor.hoverString, 0.28f));
        draw->AddCircle(center, radius, stringInk(editor.hoverString), 0, 1.5f * s);
        const char* fret = TextFormat("%d", editor.newNoteFret);
        const float size = radius * 1.05f;
        draw->AddText(fonts.bold, size, ImVec2(center.x - textWidth(fonts.bold, size, fret) / 2, center.y - size / 2 - s), uiColor(UiColor::Ink), fret);
        draw->AddText(fonts.mono, 12 * s, ImVec2(center.x + radius + 5 * s, center.y - radius - 4 * s), uiColor(UiColor::Ink), pitchText(pitch));
        hoverTip = TextFormat("Click  place fret %d (%s)      Wheel  another fret      Drag right  hold it      Shift + drag  select      Shift + wheel  scroll",
                              editor.newNoteFret, pitchText(pitch));
    }

    // The playhead's line goes under the notes, so one sitting on it can still be read
    const float playheadX = tickToX(playhead);
    const ImU32 accent = uiColor(UiColor::Accent);
    verticalLine(draw, playheadX, min.y, rowsBottom, 2.0f * s, accent);

    // Notes: a tail for as long as each is held, its fret on its string's color; the selected ones ringed
    const double margin = (radius + 16 * s) / editor.pixelsPerBeat * resolution;
    for (const FrettedNote& note : track().notes){
        if (note.tick > viewEndTick + margin) break;
        if (note.tick + note.duration < editor.viewStartTick - margin) continue;
        const NoteKey key(note.tick, note.stringIndex);
        const ImVec2 center(tickToX(note.tick), rowY(note.stringIndex));
        const bool selected = isSelected(note), under = (overNote || overGrip) && key == hoverKey;
        if (note.duration > 0){
            draw->AddRectFilled(ImVec2(center.x, center.y - 4.5f * s), ImVec2(tickToX(note.tick + note.duration), center.y + 4.5f * s),
                                stringInk(note.stringIndex, 0.55f), 4.5f * s);
        }
        if (note.duration > 0 || (selected && editor.selection.size() == 1)){
            bool grabbed = (overGrip && under) || (editor.drag == Drag::Length && key == editor.dragNote);
            float x = gripX(note);
            draw->AddRectFilled(ImVec2(x - 2 * s, center.y - 8 * s), ImVec2(x + 2 * s, center.y + 8 * s),
                                grabbed ? uiColor(UiColor::Accent) : stringInk(note.stringIndex, selected ? 1.0f : 0.8f), 2 * s);
        }
        draw->AddCircleFilled(center, radius + 1.5f * s, uiColor(UiColor::Card)); // a rim, so notes side by side stay apart
        draw->AddCircleFilled(center, radius, stringInk(note.stringIndex));
        if (selected) draw->AddCircle(center, radius + 3.5f * s, uiColor(UiColor::Accent), 0, 2.5f * s);
        else if (under) draw->AddCircle(center, radius + 3.0f * s, uiColor(UiColor::Ink, 0.5f), 0, 1.5f * s);
        const char* fret = TextFormat("%d", note.fret);
        const float size = radius * 1.05f;
        draw->AddText(fonts.bold, size, ImVec2(center.x - textWidth(fonts.bold, size, fret) / 2, center.y - size / 2 - s), IM_COL32_WHITE, fret);
        if (under && editor.drag == Drag::None){
            int pitch = track().tuning[note.stringIndex] + note.fret;
            if (overGrip) hoverTip = "Drag  how long the note is held";
            else hoverTip = TextFormat("%s: fret %d on the %s string, at %s      Wheel  its fret      Drag  move it      Right click  delete      Ctrl + click  select more",
                                       pitchText(pitch), note.fret, pitchClassName(track().tuning[note.stringIndex]), positionText(note.tick));
        }
    }

    if (editor.drag == Drag::Box){
        ImVec2 a(tickToX(editor.dragFromTick), editor.dragFrom.y), b = mouse;
        draw->AddRectFilled(a, b, uiColor(UiColor::Accent, 0.12f));
        draw->AddRect(a, b, uiColor(UiColor::Accent), 0.0f, 0, 1.0f);
    }

    draw->AddTriangleFilled(ImVec2(playheadX - 7 * s, min.y), ImVec2(playheadX + 7 * s, min.y), ImVec2(playheadX, min.y + 10 * s), accent);
    draw->PopClipRect();

    // Left of the grid: each string's open note, beside its color
    verticalLine(draw, gridLeft, min.y, rowsBottom, 1.0f, uiColor(UiColor::StaffLine));
    for (int i = 0; i < strings; i++){
        float top = rowsTop + stringToRow(i) * rowHeight;
        draw->AddRectFilled(ImVec2(min.x, top + 4 * s), ImVec2(min.x + 4 * s, top + rowHeight - 4 * s), stringInk(i), 2 * s);
        draw->AddText(fonts.bold, 15 * s, ImVec2(min.x + 16 * s, rowY(i) - 9 * s), uiColor(UiColor::Ink, i == editor.hoverString ? 1.0f : 0.75f),
                      pitchText(track().tuning[i]));
    }
    draw->AddText(fonts.mono, 10.5f * s, ImVec2(min.x + 16 * s, min.y + 8 * s), uiColor(UiColor::Dim), "BAR");
    if (wave) draw->AddText(fonts.mono, 10.5f * s, ImVec2(min.x + 16 * s, rulerBottom + waveHeight / 2 - 6 * s), uiColor(UiColor::Dim), "SONG");

    if (overTop && editor.drag == Drag::None) hoverTip = "Click or drag  move the playhead      Wheel  along the song      Ctrl + wheel  zoom";
    if (overGrip || editor.drag == Drag::Length) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
    else if (overNote) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
}

// The whole song, small: its waveform, its notes, the part on screen (dragged, it moves the view) and the playhead
static void drawOverview(ImVec2 min, ImVec2 max, float s){
    const Chart& chart = editor.chart;
    const int strings = (int)track().tuning.size();
    const float width = max.x - min.x, height = max.y - min.y;
    const double total = std::max(1, chart.endTick);
    auto tickAt = [&](float x){ return (double)(x - min.x) / width * total; };
    auto xAt = [&](double tick){ return min.x + (float)(tick / total) * width; };

    ImGui::SetCursorScreenPos(min);
    ImGui::InvisibleButton("overview", ImVec2(width, height));
    if (ImGui::IsItemHovered()) hoverTip = "The whole song: click or drag to go there";
    if (ImGui::IsItemActive()){
        editor.viewStartTick = tickAt(ImGui::GetIO().MousePos.x) - visibleTicks() / 2;
        clampView();
    }
    if (ImGui::IsItemHovered() && ImGui::GetIO().MouseWheel != 0.0f){
        editor.viewStartTick -= ImGui::GetIO().MouseWheel * WHEEL_SCROLL * s / editor.pixelsPerBeat * chart.resolution;
        clampView();
    }

    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(min, max, uiColor(UiColor::Card), 6 * s);
    draw->PushClipRect(min, max, true);
    if (waveform.show) drawWaveform(draw, min.x, max.x, min.y + 3 * s, max.y - 3 * s, uiColor(UiColor::Ink, 0.2f), tickAt);
    const bool lowOnTop = editor.settings->lowStringOnTop;
    for (const FrettedNote& note : track().notes){
        int row = lowOnTop ? note.stringIndex : strings - 1 - note.stringIndex;
        float x = xAt(note.tick), y = min.y + 5 * s + (height - 10 * s) * (row + 0.5f) / strings;
        draw->AddRectFilled(ImVec2(x, y - 1.5f * s), ImVec2(x + std::max(2.0f * s, xAt(note.tick + note.duration) - x), y + 1.5f * s), stringInk(note.stringIndex));
    }
    float from = xAt(std::max(0.0, editor.viewStartTick)), to = std::max(from + 4 * s, xAt(std::min(total, editor.viewStartTick + visibleTicks())));
    draw->AddRectFilled(ImVec2(from, min.y), ImVec2(to, max.y), uiColor(UiColor::Accent, 0.14f), 4 * s);
    draw->AddRect(ImVec2(from, min.y), ImVec2(to, max.y), uiColor(UiColor::Accent), 4 * s, 0, 1.5f * s);
    int playhead = editor.playing ? (int)secondsToTick(chart, playbackSeconds()) : editor.playheadTick;
    verticalLine(draw, xAt(playhead), min.y, max.y, 2.0f * s, uiColor(UiColor::Accent));
    draw->PopClipRect();
}

// --- Keys ---------------------------------------------------------------------------------------------

static void movePlayhead(int tick){
    editor.playheadTick = std::clamp(tick, 0, editor.chart.endTick);
    showTick(editor.playheadTick);
    if (editor.playing){ // playback jumps there
        stopPlayback();
        startPlayback();
    }
}

// Keyboard editing, skipped while a text field is being typed in
static void handleEditingKeys(){
    ImGuiIO& io = ImGui::GetIO();
    if (io.WantTextInput) return;
    const Chart& chart = editor.chart;
    const bool ctrl = io.KeyCtrl;

    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_Z)){
        if (io.KeyShift) stepHistory(editor.redoStack, editor.undoStack);
        else stepHistory(editor.undoStack, editor.redoStack);
    }
    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_Y)) stepHistory(editor.redoStack, editor.undoStack);
    if (ImGui::IsKeyPressed(ImGuiKey_Space, false)){
        if (editor.playing) stopPlayback();
        else startPlayback();
    }
    if (ImGui::IsKeyPressed(ImGuiKey_F1, false)) editor.openKeys = true;

    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_A, false)){
        editor.selection.clear();
        for (const FrettedNote& note : track().notes) editor.selection.insert({note.tick, note.stringIndex});
    }
    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_C, false)) copySelection();
    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_X, false)){
        copySelection();
        deleteSelection();
    }
    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_V, false)) pasteAtPlayhead();
    if (ImGui::IsKeyPressed(ImGuiKey_Delete) || ImGui::IsKeyPressed(ImGuiKey_Backspace)) deleteSelection();

    // A fret typed: one digit, or two close together (1 then 2 is 12)
    for (int digit = 0; digit <= 9 && !ctrl; digit++){
        if (!ImGui::IsKeyPressed((ImGuiKey)(ImGuiKey_0 + digit), false) && !ImGui::IsKeyPressed((ImGuiKey)(ImGuiKey_Keypad0 + digit), false)) continue;
        int fret = digit;
        bool second = editor.typedDigit > 0 && GetTime() - editor.typedAt < FRET_TYPING_S && editor.typedDigit * 10 + digit <= MAX_FRET;
        if (second) fret = editor.typedDigit * 10 + digit;
        editor.typedDigit = second ? -1 : digit;
        editor.typedAt = GetTime();
        changeFrets(0, true, fret);
    }

    // Up and Down: the fret. With Ctrl: the same notes on the string above or below, as the rows are drawn.
    int vertical = (ImGui::IsKeyPressed(ImGuiKey_UpArrow) ? 1 : 0) - (ImGui::IsKeyPressed(ImGuiKey_DownArrow) ? 1 : 0);
    if (vertical != 0 && ctrl) restringSelection(editor.settings->lowStringOnTop ? -vertical : vertical);
    else if (vertical != 0) changeFrets(vertical, false, 0);

    // Left and Right: the selected notes along the grid (with Shift, how long they're held), or else the playhead.
    // With Ctrl: the playhead, a bar at a time.
    int along = (ImGui::IsKeyPressed(ImGuiKey_RightArrow) ? 1 : 0) - (ImGui::IsKeyPressed(ImGuiKey_LeftArrow) ? 1 : 0);
    if (along != 0 && ctrl){
        int bar = barNumberAt(chart, editor.playheadTick);
        bool onBarLine = barStartTick(chart, bar) == editor.playheadTick;
        movePlayhead(barStartTick(chart, along > 0 ? bar + 1 : onBarLine ? std::max(0, bar - 1) : bar));
    } else if (along != 0 && editor.selection.empty()){
        movePlayhead(snapTick(editor.playheadTick) + along * snapStep());
    } else if (along != 0 && io.KeyShift){
        changeLengths(along * snapStep());
    } else if (along != 0){
        if (moveNotes(std::vector<FrettedNote>(track().notes), std::set<NoteKey>(editor.selection), along * snapStep(), 0)) markChangedInRun();
        showTick(editor.selection.begin()->first);
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Home, false)) movePlayhead(0);
    if (ImGui::IsKeyPressed(ImGuiKey_End, false)) movePlayhead(chart.endTick);

    if (ImGui::IsKeyPressed(ImGuiKey_Equal) || ImGui::IsKeyPressed(ImGuiKey_KeypadAdd)) zoomBy(1.3f, editor.gridWidth / 2);
    if (ImGui::IsKeyPressed(ImGuiKey_Minus) || ImGui::IsKeyPressed(ImGuiKey_KeypadSubtract)) zoomBy(1.0f / 1.3f, editor.gridWidth / 2);
}

// Everything the mouse and the keyboard do, on one card (F1)
static void drawKeysPopup(float s){
    struct Row { const char* keys; const char* what; };
    static const Row MOUSE[] = {
        { "Click", "place a note; drag right to hold it longer" },
        { "Wheel", "the fret: of the note under the mouse, or of the next one" },
        { "Drag a note", "move it along the song, or to another string" },
        { "Drag its end", "how long it's held" },
        { "Right click", "delete; held, everything it passes over" },
        { "Ctrl + click", "add a note to the selection, or take it out" },
        { "Shift + drag", "select every note in a box" },
        { "Shift + wheel", "along the song (or the middle button, dragged)" },
        { "Ctrl + wheel", "zoom" },
        { "Ruler, waveform", "click or drag to move the playhead" },
    };
    static const Row KEYS[] = {
        { "0 - 9", "type a fret: 1 then 2 is 12" },
        { "Up  Down", "fret up or down" },
        { "Ctrl + Up  Down", "the same notes on the next string" },
        { "Left  Right", "move the selected notes; with none, the playhead" },
        { "Shift + Left  Right", "held shorter or longer" },
        { "Ctrl + Left  Right", "the playhead, bar by bar" },
        { "Delete", "delete the selected notes" },
        { "Ctrl + A  C  X  V", "select all, copy, cut, paste at the playhead" },
        { "Ctrl + Z  Y", "undo, redo" },
        { "Space   Home  End", "play or stop; the playhead to the start, the end" },
        { "+  -", "zoom" },
        { "F5   Ctrl + S", "test play from the playhead; save" },
    };

    if (editor.openKeys) ImGui::OpenPopup(KEYS_POPUP);
    editor.openKeys = false;
    const UiFonts& fonts = uiFonts();
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(28 * s, 22 * s));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(10 * s, 7 * s));
    if (ImGui::BeginPopupModal(KEYS_POPUP, nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove)){
        auto column = [&](const char* title, const Row* rows, int count, float keysWidth){
            ImGui::BeginGroup();
            ImGui::PushFont(fonts.mono, 13 * s);
            ImGui::TextColored(uiColorVec(UiColor::Accent), "%s", title);
            ImGui::PopFont();
            ImGui::Dummy(ImVec2(0, 2 * s));
            for (int i = 0; i < count; i++){
                ImGui::PushFont(fonts.mono, 13.5f * s);
                ImGui::TextColored(uiColorVec(UiColor::Ink), "%s", rows[i].keys);
                ImGui::PopFont();
                ImGui::SameLine(keysWidth * s); // from the group's left
                ImGui::PushFont(fonts.text, 15 * s);
                ImGui::TextColored(uiColorVec(UiColor::Dim), "%s", rows[i].what);
                ImGui::PopFont();
            }
            ImGui::EndGroup();
        };
        column("MOUSE", MOUSE, (int)(sizeof(MOUSE) / sizeof(MOUSE[0])), 150);
        ImGui::SameLine(0, 44 * s);
        column("KEYBOARD", KEYS, (int)(sizeof(KEYS) / sizeof(KEYS[0])), 180);
        ImGui::Dummy(ImVec2(0, 8 * s));
        pushCompactStyle(s);
        if (ImGui::Button("Close") || ImGui::IsKeyPressed(ImGuiKey_Escape, false) || ImGui::IsKeyPressed(ImGuiKey_F1, false)
            || ImGui::IsKeyPressed(ImGuiKey_Enter, false)) ImGui::CloseCurrentPopup();
        popCompactStyle();
        ImGui::EndPopup();
    }
    ImGui::PopStyleVar(2);
}

// --- Screen -------------------------------------------------------------------------------------------

bool openEditor(const SongEntry& song, const std::string& userSongsDir, const std::string& packagesDir, Settings& settings,
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
    fresh.settings = &settings;
    fresh.builtIn = song.builtIn;
    if (song.builtIn) fresh.status = "Built-in song: saving creates your own copy";
    fresh.active = true;
    fresh.committed = fresh.chart;
    fresh.viewStartTick = -1.0e9; // as far left as the view goes (clampView): a little before the first bar
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
    setSongVolume(settings.editorSongVolume);

    // Arrow keys edit notes here instead of moving between buttons
    ImGui::GetIO().ConfigFlags &= ~ImGuiConfigFlags_NavEnableKeyboard;
    return true;
}

void closeEditor(){
    if (!editor.active) return;
    stopPlayback();
    unloadSong();
    setSongVolume(1.0f); // the editor's song volume is its own
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
    setSongVolume(editor.settings->editorSongVolume);
}

EditorChoice editorScreen(){
    EditorChoice choice = EditorChoice::None;
    ImGuiIO& io = ImGui::GetIO();
    const float s = menuScale(), width = io.DisplaySize.x, height = io.DisplaySize.y;
    hoverTip.clear();
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::Begin("Editor", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings
                                    | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar();
    // Keys go to a popup while one is open (the keys' list, a dropdown, the question before leaving), not to the notes
    const bool popupOpen = ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);

    bool requestBack = false, requestTestPlay = false;
    drawTopBar(s, width, requestBack, requestTestPlay);
    drawToolBar(s, width);
    const float top = (TOP_BAR + TOOL_BAR) * s, bottom = height - BOTTOM_BAR * s;
    const float right = width - (editor.drawerOpen ? DRAWER_WIDTH * s : 0.0f);
    const float overviewTop = bottom - (OVERVIEW_HEIGHT + 10) * s;
    drawTimeline(ImVec2(0, top), ImVec2(right, overviewTop - 10 * s), s);
    drawOverview(ImVec2(editor.gridLeft, overviewTop), ImVec2(right - 12 * s, overviewTop + OVERVIEW_HEIGHT * s), s);
    if (editor.drawerOpen) drawDetails(ImVec2(right, top), ImVec2(width, bottom), s);
    drawBottomBar(s, width, height, "Space  play      Wheel on a string  the fret of the next note      Shift + wheel  along the song      Ctrl + wheel  zoom      F1  every key");
    drawKeysPopup(s);

    // Nothing held, and no run of wheel notches or key repeats under way: what changed is one undo step
    if (!ImGui::IsAnyItemActive() && GetTime() >= editor.settleUntil) commitChange();
    if (editor.playing){
        schedulePlayback();
        // Past the end of both the chart and the song, playback stops on its own
        double end = std::max(tickToSeconds(editor.chart, editor.chart.endTick), editor.songLoaded ? songLength() : 0.0);
        if (playbackSeconds() > end + 0.5) stopPlayback();
    }

    if (!popupOpen){
        handleEditingKeys();
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S, false)) saveEditorChart();
        if (ImGui::IsKeyPressed(ImGuiKey_F5, false)) requestTestPlay = true;
        if (ImGui::IsKeyPressed(ImGuiKey_Escape, false) && !io.WantTextInput) requestBack = true;
    }
    if (requestTestPlay){
        if (editor.chart.audioFile.empty() || !editor.songLoaded) editor.status = "Test play needs the song's audio";
        else {
            stopPlayback();
            setSongVolume(1.0f); // the game plays it as it is
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
