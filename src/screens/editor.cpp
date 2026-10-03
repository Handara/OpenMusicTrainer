#include "screens/editor.h"

#include "app/filedialog.h"
#include "app/videoconvert.h"
#include "audio/audio.h"
#include "core/addon.h"
#include "core/beats.h"
#include "core/chart.h"
#include "core/files.h"
#include "core/guitarpro.h"
#include "core/songpackage.h"
#include "core/music.h"
#include "core/take.h"
#include "imgui.h"
#include "imgui_stdlib.h"
#include "input/noteinput.h"
#include "raylib.h"
#include "screens/tonewizard.h"
#include "ui/menulist.h"
#include "ui/settingsui.h"
#include "ui/theme.h"
#include "ui/ui.h"
#include "video/video.h"
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
const float NAMED_NOTE_RADIUS = 18.0f;   // with its name under its fret, inside it
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
const double MIN_TRIMMED_S = 0.5;        // a trimmed song keeps at least this much of its audio
const float TRIM_GRIP = 7.0f;            // pixels either side of a trim edge that take hold of it
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
const char* const PART_POPUP = "This part";
const char* const IMPORT_POPUP = "Import into this song";
const char* const IMPORT_PARTS_POPUP = "Parts to bring in";
const char* const EXPORT_POPUP = "Export this song";
const char* const VIDEO_POPUP = "Bringing in a video";
const char* const TEMPO_POPUP = "Listening to the song";
const int LISTEN_RATE = 22050;        // the song is listened to at this rate for its beat: plenty, and quick
const std::vector<std::string> VIDEO_PATTERNS = { "*.mp4", "*.m4v", "*.mkv", "*.webm", "*.mov", "*.avi", "*.wmv", "*.flv", "*.mpg", "*.mpeg" };
const std::vector<std::string> PARTS_PATTERNS = { "*.gp", "*.gpx", "*.gp5", "*.gp4", "*.gp3", "*.chart" };
const std::vector<std::string> AUDIO_PATTERNS = { "*.mp3", "*.ogg", "*.flac", "*.wav" };
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
    Playhead,  // on the ruler
    Slide,     // on the waveform: a click moves the playhead, moving slides the song under the grid (its offset)
    TrimStart, // the waveform's edges: where the song starts and ends in its audio
    TrimEnd,
    Pan,       // the middle button: the view follows the mouse
};

struct EditorState {
    Chart chart;
    std::string chartPath;
    std::string songFolder;
    std::string userSongsDir;
    std::string packagesDir;  // where Export writes the song's package
    std::string addonsDir;    // where add-ons are installed: the video add-on's FFmpeg is looked for there
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
    bool openImport = false, openExport = false, openImportParts = false; // and these popups

    // Import: a tab or another song's chart, read, waiting for which of its parts to bring in
    Chart importChart;
    std::string importName;               // the file it's from
    std::vector<std::string> importLeftOut; // what a tab holds that couldn't be read
    std::vector<char> importChosen;       // per part: brought in or not
    bool importBars = false;              // its tempos, time signatures and keys too
    bool importVideoSound = false;        // a video brought in: its sound too, as the song's audio

    // The song's beats, as last found from its sound (core/beats), and the one bar 1 is laid on: kept so the bars
    // can be moved a beat, or the tempo halved or doubled, without listening again
    std::vector<double> foundBeats;
    int foundFirst = 0;
    bool findTempoNext = false;           // asked for while something else was at work: started once that's done
    bool exportVideo = true;              // the package made with the song's video in it

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
    double slideFromOffset = 0.0; // Slide: the chart's offset as the drag began
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

    // Recording (R): the part played on its instrument while the song plays, written down as it's heard
    bool recording = false;
    Take take;                 // what this take has written so far (core/take)

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

// A video brought into the song is converted on a thread of its own (FFmpeg takes tens of seconds over a long
// one); the editor shows how far along it is, and takes what was made once it's done.
static struct {
    std::thread thread;
    std::atomic<bool> working{false};
    std::atomic<bool> cancel{false};
    std::atomic<bool> done{false};     // set last by the thread: what's below is then the main thread's to read
    std::atomic<int> stage{0};         // 0 its sound, 1 its pictures
    std::atomic<float> progress{0.0f};
    std::string file;                  // the video's name, for the screen
    std::string videoName, audioName;  // what was made, in the song's folder ("" for none)
    std::string error;
    bool ok = false;
} bringingVideo;

// The song listened to for its beat, on a thread too: reading the whole of it takes a second or two
static struct {
    std::thread thread;
    std::atomic<bool> working{false};
    std::atomic<bool> cancel{false};
    std::atomic<bool> done{false};     // set last by the thread: what's below is then the main thread's to read
    SongBeats found;
    std::string error;
    bool ok = false;
} findingTempo;

static void stopTempoWork(){
    findingTempo.cancel = true;
    if (findingTempo.thread.joinable()) findingTempo.thread.join();
    findingTempo.cancel = false;
    findingTempo.working = false;
    findingTempo.done = false;
}

static void stopVideoWork(){
    bringingVideo.cancel = true;
    if (bringingVideo.thread.joinable()) bringingVideo.thread.join();
    bringingVideo.cancel = false;
    bringingVideo.working = false;
    bringingVideo.done = false;
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

static void stopRecording();

// The part being edited is taken out of the song, its notes with it: one step to undo
static void removePart(){
    if (editor.chart.frettedTracks.size() < 2) return; // a song keeps at least one part
    stopRecording(); // a take under way is this part's
    const std::string name = track().name.empty() ? "The part" : track().name;
    editor.chart.frettedTracks.erase(editor.chart.frettedTracks.begin() + editor.part);
    choosePart(editor.part);
    markChanged();
    editor.status = name + " taken out of the song: Ctrl + Z brings it back";
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

// The audio's length, 0 without any
static double audioSeconds(){
    return editor.songLoaded ? songLength() : 0.0;
}

// Where the song ends in its audio: where it's trimmed to, or the audio's own end
static double songEndSeconds(){
    return editor.chart.trimEnd > 0.0 ? std::min(editor.chart.trimEnd, audioSeconds()) : audioSeconds();
}

// The view stays between a little before the song's start and its end: the chart's, or the audio's where it reaches
// further (it starts before the first bar when the offset says so)
static void clampView(){
    const Chart& chart = editor.chart;
    double lead = VIEW_LEAD * menuScale() / editor.pixelsPerBeat * chart.resolution;
    double first = 0.0, last = chart.endTick;
    if (editor.songLoaded){
        first = std::min(first, secondsToTick(chart, 0.0));
        last = std::max(last, secondsToTick(chart, audioSeconds()));
    }
    editor.viewStartTick = std::clamp(editor.viewStartTick, first - lead, std::max(first - lead, last));
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

static void movePlayhead(int tick){
    editor.playheadTick = std::clamp(tick, 0, editor.chart.endTick);
    showTick(editor.playheadTick);
    if (editor.playing){ // playback jumps there
        stopPlayback();
        startPlayback();
    }
}

// The song slid under the grid: its offset moved by some seconds (later in the audio = the waveform moves left).
// Playing, it starts again from the playhead, to hear how the notes and the click now sit on it.
static void restartPlayback(){
    if (!editor.playing) return;
    stopPlayback();
    startPlayback();
}

static void slideSong(double seconds){
    editor.chart.offset = std::round((editor.chart.offset + seconds) * 10000.0) / 10000.0;
    editor.status = TextFormat("Offset %.3f s: the first bar starts there in the audio", editor.chart.offset);
    markChangedInRun();
    restartPlayback();
}

// --- Recording ----------------------------------------------------------------------------------------
// The part played in (R): the song plays from the playhead, the instrument is listened to as in a song, and each
// note heard is written where it was played: on the grid's nearest step, on its likeliest string and fret, held
// until the next note or until it dies away (core/take). Strings plucked together are written together
// (core/polyphony). The whole take is one step to undo, and is left selected: to move, to delete, to play again.

// Where the song is for the player's ears: the engine's clock runs ahead of the speakers by the output's delay
static double heardSeconds(){
    return playbackSeconds() - editor.settings->globalOffsetMs / 1000.0;
}

static void stopRecording(){
    if (!editor.recording) return;
    editor.recording = false;
    endTake(editor.take, editor.chart, track(), snapStep(), heardSeconds() - editor.settings->inputOffsetMs / 1000.0);
    stopNoteInput();
    stopPlayback();
    editor.selection.clear();
    for (const Take::Written& written : editor.take.written) editor.selection.insert({ written.tick, written.stringIndex });
    pruneSelection();
    if (editor.selection.empty()){
        editor.status = "Nothing was heard: is the instrument's input the one chosen in Settings, Instruments?";
        return;
    }
    markChanged(); // the notes' lengths moved until now
    showTick(editor.selection.begin()->first); // the view followed the song: back to where the take starts
    editor.status = TextFormat("%d notes recorded and selected: Ctrl + Z takes them back", (int)editor.selection.size());
}

static void startRecording(){
    if (editor.recording) return;
    commitChange(); // what was changed before stays its own step to undo
    const FrettedTrack& part = track();
    const InputRole role = part.type == InstrumentType::Bass ? InputRole::Bass : InputRole::Guitar;
    const float lowest = midiToFrequency((float)*std::min_element(part.tuning.begin(), part.tuning.end())) * 0.9f; // a little under its lowest string
    hearInstrument(*editor.settings, role); // through its own tone, as in a song
    std::string error;
    if (!startNoteInput(editor.settings->inputDevice, lowest, error, channelFor(*editor.settings, role))){
        editor.status = "Recording: " + error + " (see Settings, Instruments)";
        return;
    }
    editor.take = Take{};
    editor.recording = true;
    if (!editor.playing) startPlayback();
    editor.status = "Recording: play the part. R or Space stops";
}

// Once a frame while recording: what the instrument played since the last one, into the part
static void recordPlayed(){
    struct Pluck {
        double seconds;
        std::vector<int> pitches;
    };
    // A note's place in the song: where the song is heard to be now, less how long ago the note started, less the
    // input's own delay (the same sum a song's judging does)
    const double now = heardSeconds() - editor.settings->inputOffsetMs / 1000.0;
    std::vector<Pluck> plucks;
    for (const PlayedNote& note : updateNoteInput()) plucks.push_back({ now - note.age, { note.pitch } });
    for (const PlayedChord& chord : noteInputChords()) plucks.push_back({ now - chord.age, chord.pitches });
    // In the order played: a chord is known later than a single note plucked after it may be
    std::stable_sort(plucks.begin(), plucks.end(), [](const Pluck& a, const Pluck& b){ return a.seconds < b.seconds; });
    for (const Pluck& pluck : plucks) takePluck(editor.take, editor.chart, track(), snapStep(), pluck.pitches, pluck.seconds);
    takeLevel(editor.take, editor.chart, track(), snapStep(), noteInputLevelDb(), now);
    if (!plucks.empty()){
        coverNotes();
        markChanged();
    }
}

// --- The song's own beat ------------------------------------------------------------------------------
// The song is listened to for its tempo and its beats (core/beats), and the chart's bars are laid on them: bar 1
// on the beat that most likely starts a bar, the tempo the song's own (one for a steady recording, following it
// beat by beat for one that drifts). Which beat starts the bar is a guess, and a tempo can be heard at half or
// twice what a musician would count: both are put right from what was found, without listening again.

// The song's length in bars, counted at the tempo it has now: to the end of its audio, and of its notes
static void fitLengthToAudio(){
    Chart& chart = editor.chart;
    int last = 1;
    if (editor.songLoaded) last = std::max(last, (int)std::ceil(secondsToTick(chart, songEndSeconds())));
    for (const FrettedTrack& part : chart.frettedTracks) if (!part.notes.empty()) last = std::max(last, part.notes.back().tick + 1);
    chart.endTick = barStartTick(chart, barNumberAt(chart, last - 1) + 1);
}

// The bars laid on the beats found, bar 1 on the beat numbered `first`
static void layBarsOnBeats(int first){
    if (editor.foundBeats.size() < 2) return;
    stopPlayback();
    editor.foundFirst = std::clamp(first, 0, (int)editor.foundBeats.size() - 2);
    fitChartToBeats(editor.chart, editor.foundBeats, editor.foundFirst);
    fitLengthToAudio();
    editor.playheadTick = std::min(editor.playheadTick, editor.chart.endTick);
    markChanged();
    const std::vector<TempoChange>& tempos = editor.chart.tempoMap;
    if (tempos.size() == 1){
        editor.status = TextFormat("The song is at %.5g BPM, steady: its bars are on its beats, bar 1 at %.3f s", tempos[0].bpm, editor.chart.offset);
    } else {
        auto slowest = std::min_element(tempos.begin(), tempos.end(), [](const TempoChange& a, const TempoChange& b){ return a.bpm < b.bpm; });
        auto fastest = std::max_element(tempos.begin(), tempos.end(), [](const TempoChange& a, const TempoChange& b){ return a.bpm < b.bpm; });
        editor.status = TextFormat("The song's tempo moves (%.4g to %.4g BPM): its bars follow it beat by beat, bar 1 at %.3f s", slowest->bpm, fastest->bpm, editor.chart.offset);
    }
}

// Half as many beats (every other one, bar 1's among them) or twice as many (one more between each two)
static void halveBeats(){
    std::vector<double> beats;
    const int start = editor.foundFirst % 2;
    for (size_t i = start; i < editor.foundBeats.size(); i += 2) beats.push_back(editor.foundBeats[i]);
    if (beats.size() < 2) return;
    int first = (editor.foundFirst - start) / 2;
    editor.foundBeats = beats;
    layBarsOnBeats(first);
}

static void doubleBeats(){
    std::vector<double> beats;
    for (size_t i = 0; i < editor.foundBeats.size(); i++){
        beats.push_back(editor.foundBeats[i]);
        if (i + 1 < editor.foundBeats.size()) beats.push_back((editor.foundBeats[i] + editor.foundBeats[i + 1]) / 2);
    }
    int first = 2 * editor.foundFirst;
    editor.foundBeats = beats;
    layBarsOnBeats(first);
}

static void findTempo(){
    if (findingTempo.working) return;
    if (!editor.songLoaded || editor.chart.audioFile.empty()){
        editor.status = "The tempo is found from the song's audio, and this song has none";
        return;
    }
    if (bringingVideo.working){ // one thing at a time: a video being brought in may be about to change the audio
        editor.findTempoNext = true;
        return;
    }
    stopTempoWork();
    findingTempo.working = true;
    const std::string audioPath = (fs::path(editor.songFolder) / editor.chart.audioFile).string();
    const int beatsPerBar = editor.chart.timeSignatures[0].beats;
    findingTempo.thread = std::thread([audioPath, beatsPerBar]{
        std::vector<float> samples;
        std::string error;
        SongBeats found;
        bool ok = decodeAudioFile(audioPath, LISTEN_RATE, 1, samples, error, findingTempo.cancel);
        if (ok && !findSongBeats(samples, LISTEN_RATE, beatsPerBar, found, findingTempo.cancel)){
            ok = false;
            error = "no steady beat could be heard in it";
        }
        findingTempo.ok = ok;
        findingTempo.error = error;
        findingTempo.found = found;
        findingTempo.done = true;
    });
}

// The listening done: the bars go on the beats found
static void takeTempoWork(){
    if (editor.findTempoNext && !bringingVideo.working && !findingTempo.working){
        editor.findTempoNext = false;
        findTempo();
    }
    if (!findingTempo.done) return;
    if (findingTempo.thread.joinable()) findingTempo.thread.join();
    const bool stopped = findingTempo.cancel;
    findingTempo.done = false;
    findingTempo.working = false;
    findingTempo.cancel = false;
    if (!findingTempo.ok){
        editor.status = stopped ? "The tempo wasn't looked for" : "The song's tempo: " + findingTempo.error;
        return;
    }
    editor.foundBeats = findingTempo.found.beats;
    layBarsOnBeats(findingTempo.found.downbeat);
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

// --- Import and export --------------------------------------------------------------------------------
// A song is pieces that come and go on their own: its parts (the notes, with the bars and tempos under them) and its
// audio. Import brings one in from a file, into the song being edited; Export writes the song out whole, or its
// chart alone for someone who has the recording.

static std::string lowerExtension(const std::string& path){
    std::string extension = fs::path(path).extension().string();
    for (char& c : extension) c = (char)std::tolower((unsigned char)c);
    return extension;
}

static bool matches(const std::string& path, const std::vector<std::string>& patterns){
    std::string extension = lowerExtension(path);
    for (const std::string& pattern : patterns) if (extension == pattern.substr(1)) return true; // "*.gp" is ".gp"
    return false;
}

// A tab (Guitar Pro) or a lahn chart: read, then its parts are offered (drawImportPopups)
static void readPartsFile(const std::string& path){
    const std::string name = fs::path(path).filename().string();
    Chart chart;
    std::vector<std::string> leftOut;
    std::string error;
    bool ok;
    if (lowerExtension(path) == ".chart"){
        ok = loadChart(path, chart, error);
        if (!ok && error.rfind(path, 0) == 0) error = error.substr(path.size() + 1); // the line and why, without the path
    } else {
        GuitarProImport tab;
        ok = importGuitarPro(path, tab, error);
        chart = tab.chart;
        leftOut = tab.leftOut;
    }
    if (ok && chart.frettedTracks.empty()){
        ok = false;
        error = "no guitar or bass part in it";
    }
    if (!ok){
        editor.status = name + ": " + error;
        return;
    }
    editor.importChart = chart;
    editor.importName = name;
    editor.importLeftOut = leftOut;
    editor.importChosen.assign(chart.frettedTracks.size(), 1);
    editor.importBars = true; // the file says how its music is counted: the song follows it, unless told not to
    editor.openImportParts = true;
}

// The parts chosen come in after the song's own, as one step to undo
static void bringPartsIn(){
    std::vector<int> parts;
    for (int i = 0; i < (int)editor.importChosen.size(); i++) if (editor.importChosen[i]) parts.push_back(i);
    if (parts.empty()) return;
    const int first = (int)editor.chart.frettedTracks.size();
    stopPlayback();
    importParts(editor.chart, editor.importChart, parts, editor.importBars);
    // The song's length in bars was counted at its old tempo: at the new one it's as long as its audio again (and
    // no shorter than its notes need)
    if (editor.importBars && editor.songLoaded) fitLengthToAudio();
    choosePart(first);
    editor.playheadTick = std::min(editor.playheadTick, editor.chart.endTick);
    markChanged();
    editor.status = TextFormat("%d %s brought in from %s", (int)parts.size(), parts.size() == 1 ? "part" : "parts", editor.importName.c_str());
    if (editor.importBars){
        const TimeSignatureChange& time = editor.chart.timeSignatures[0];
        editor.status += TextFormat(": the song is now at %.5g BPM in %d/%d", editor.chart.tempoMap[0].bpm, time.beats, time.beatUnit);
        if (editor.songLoaded) editor.status += ". Drag the waveform so its first bar starts on bar 1";
    }
    editor.importChart = Chart{};
}

// A name no file in the song's folder has yet: "audio.mp3", else "audio 2.mp3"...
static std::string freeName(const std::string& stem, const std::string& extension){
    std::string name = stem + extension;
    std::error_code ec;
    for (int n = 2; fs::exists(fs::path(editor.songFolder) / name, ec); n++) name = stem + " " + std::to_string(n) + extension;
    return name;
}

// The song's audio becomes a file already in its folder. The audio it had stays there, so undoing goes back to it.
static bool switchAudio(const std::string& name, std::string& error){
    const fs::path folder = editor.songFolder;
    stopPlayback();
    stopWaveform();
    unloadSong();
    editor.songLoaded = loadSong((folder / name).string(), error);
    if (!editor.songLoaded){
        // Back to the audio it had
        std::string ignored;
        if (!editor.chart.audioFile.empty()){
            editor.songLoaded = loadSong((folder / editor.chart.audioFile).string(), ignored);
            if (editor.songLoaded) startWaveform((folder / editor.chart.audioFile).string());
        }
        return false;
    }
    startWaveform((folder / name).string());
    setSongVolume(editor.settings->editorSongVolume);
    editor.chart.audioFile = name;
    markChanged();
    return true;
}

static void importAudio(const std::string& path){
    if (editor.builtIn){
        editor.status = "Save first: a built-in song becomes your own copy, and the audio goes into that";
        return;
    }
    const std::string name = freeName("audio", lowerExtension(path));
    std::error_code ec;
    fs::copy_file(path, fs::path(editor.songFolder) / name, ec);
    if (ec){
        editor.status = "Could not copy the audio: " + ec.message();
        return;
    }
    std::string error;
    if (!switchAudio(name, error)){
        fs::remove(fs::path(editor.songFolder) / name, ec);
        editor.status = fs::path(path).filename().string() + ": " + error;
        return;
    }
    editor.status = "The song's audio is now " + fs::path(path).filename().string() + ": drag the waveform to line it up with the bars";
}

// The song's video, from a file: shown behind the notes when the song is played. One lahn plays as it is (.mpg) is
// copied in; any other kind is converted by FFmpeg (app/videoconvert), on a thread, its sound taken as the song's
// audio if that's asked.
static void importVideo(const std::string& path, bool withSound){
    if (bringingVideo.working) return;
    if (editor.builtIn){
        editor.status = "Save first: a built-in song becomes your own copy, and the video goes into that";
        return;
    }
    const std::string shown = fs::path(path).filename().string();
    const fs::path folder = editor.songFolder;
    const std::string videoName = freeName("video", ".mpg");
    if (isPlayableVideo(path) && !withSound){
        std::error_code ec;
        std::string error;
        fs::copy_file(path, folder / videoName, ec);
        if (ec || !openSongVideo((folder / videoName).string(), error)){ // opened once, to know it plays
            editor.status = shown + ": " + (ec ? ec.message() : error);
            fs::remove(folder / videoName, ec);
            return;
        }
        closeSongVideo();
        editor.chart.videoFile = videoName;
        editor.chart.videoOffset = 0.0;
        markChanged();
        editor.status = "The song's video is now " + shown;
        return;
    }
    const std::string ffmpeg = findFfmpeg(editor.addonsDir);
    if (ffmpeg.empty()){
        editor.status = "A video of this kind needs lahn's video add-on: drop its file (lahn-video...lahnaddon) on the editor, then the video again";
        return;
    }
    stopVideoWork();
    bringingVideo.file = shown;
    bringingVideo.stage = withSound ? 0 : 1;
    bringingVideo.progress = 0.0f;
    bringingVideo.working = true;
    const std::string videoTo = (folder / videoName).string();
    const std::string audioTo = withSound ? (folder / freeName("audio", ".mp3")).string() : "";
    bringingVideo.thread = std::thread([path, ffmpeg, videoTo, audioTo]{
        std::string error, audioName;
        bool ok = true;
        if (!audioTo.empty()){
            if (extractAudio(ffmpeg, path, audioTo, &bringingVideo.progress, bringingVideo.cancel, error)) audioName = fs::path(audioTo).filename().string();
            else if (error != "the video has no sound") ok = false; // one with none still has its pictures
        }
        if (ok){
            bringingVideo.stage = 1;
            bringingVideo.progress = 0.0f;
            error.clear();
            ok = convertVideo(ffmpeg, path, videoTo, &bringingVideo.progress, bringingVideo.cancel, error);
            std::error_code ec;
            if (!ok && !audioName.empty()) fs::remove(audioTo, ec); // all of it, or none
        }
        bringingVideo.ok = ok;
        bringingVideo.error = error;
        bringingVideo.videoName = fs::path(videoTo).filename().string();
        bringingVideo.audioName = audioName;
        bringingVideo.done = true;
    });
}

// The conversion done: what it made becomes the song's
static void takeVideoWork(){
    if (!bringingVideo.done) return;
    if (bringingVideo.thread.joinable()) bringingVideo.thread.join();
    bringingVideo.done = false;
    bringingVideo.working = false;
    if (!bringingVideo.ok){
        editor.status = bringingVideo.error == "stopped" ? "The video wasn't brought in" : bringingVideo.file + ": " + bringingVideo.error;
        return;
    }
    editor.chart.videoFile = bringingVideo.videoName;
    editor.chart.videoOffset = 0.0;
    markChanged();
    editor.status = "The song's video is now " + bringingVideo.file;
    if (!bringingVideo.audioName.empty()){
        std::string error;
        if (switchAudio(bringingVideo.audioName, error)) editor.status += ", and its sound the song's audio: drag the waveform to line it up with the bars";
        else editor.status += " (its sound couldn't be read: " + error + ")";
    }
}

// An add-on's file dropped on the editor is installed (the video add-on, to bring videos in)
static void installDroppedAddon(const std::string& path){
    AddonInfo info;
    std::string error;
    if (installAddon(path, editor.addonsDir, info, error)) editor.status = "The " + info.name + " add-on is installed";
    else editor.status = error;
}

// A file chosen or dropped: what it is says what's brought in
static void importFile(const std::string& path){
    if (matches(path, AUDIO_PATTERNS)) importAudio(path);
    else if (matches(path, PARTS_PATTERNS)) readPartsFile(path);
    else if (isVideoFile(path)) importVideo(path, editor.chart.audioFile.empty()); // its sound too, for a song with no audio
    else if (lowerExtension(path) == ADDON_EXTENSION) installDroppedAddon(path);
    else editor.status = fs::path(path).filename().string() + " is neither a tab, a chart, an audio file nor a video";
}

// The song as one file anyone can install (core/songpackage), in the packages folder, which then opens. It's made
// from the saved song, so unsaved changes have to be saved first.
static void exportPackage(){
    if (editor.dirty){
        editor.status = "Save first: the package is made from the saved song";
        return;
    }
    std::error_code ec;
    fs::create_directories(editor.packagesDir, ec);
    std::string name = safeFolderName(editor.chart.title);
    fs::path package = fs::path(editor.packagesDir) / ((name.empty() ? "Song" : name) + SONG_PACKAGE_EXTENSION);
    std::string error;
    if (!exportSongPackage(editor.songFolder, package.string(), error, editor.exportVideo)){
        editor.status = "Could not export: " + error;
        return;
    }
    editor.status = "Package ready: " + package.filename().string();
    openFolder(editor.packagesDir);
}

// Its chart alone, beside the packages: the parts, the bars and the tempos, for someone who has the recording.
// They bring it into a song of their own with Import.
static void exportChart(){
    std::error_code ec;
    fs::create_directories(editor.packagesDir, ec);
    std::string name = safeFolderName(editor.chart.title);
    fs::path file = fs::path(editor.packagesDir) / ((name.empty() ? "Song" : name) + ".chart");
    std::string error;
    if (!saveChart(file.string(), editor.chart, error)){
        editor.status = "Could not export: " + error;
        return;
    }
    editor.status = "Chart ready: " + file.filename().string();
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

enum class Icon { None, Back, Play, Stop, Record, Plus, Minus, Lamp, LampLit };

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
        // Recording is the one thing that fills its button red
        draw->AddRectFilled(min, max, uiColor(icon == Icon::Record ? UiColor::Bad : UiColor::Accent, held ? 0.8f : 1.0f), height / 2);
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
        case Icon::Record: draw->AddCircleFilled(ImVec2(x + half, y), half + s, on ? ink : uiColor(UiColor::Bad, alpha)); break;
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
    if (barButton(right, "Export", nullptr, "The song as one file anyone can install, or its chart alone")) editor.openExport = true;
    if (barButton(right, "Import", nullptr, "Bring into this song: parts from a tab or another song, or its audio")) editor.openImport = true;
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
    if (barButton(bar, editor.recording ? "Recording" : "Record", "R", "Play the part on your instrument over the song: every note you play is written where you played it",
                  editor.recording, true, Icon::Record, "Recording")){
        if (editor.recording) stopRecording();
        else startRecording();
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

    // The song's parts: the one being edited, adding one, and taking the one being edited out (a right click on a
    // part does that too)
    const bool severalParts = chart.frettedTracks.size() > 1;
    bool openPartMenu = false;
    ImVec2 partMenuAt;
    for (int i = 0; i < (int)chart.frettedTracks.size(); i++){
        const FrettedTrack& part = chart.frettedTracks[i];
        const float partX = bar.x;
        ImGui::PushID(i);
        if (barButton(bar, part.name.empty() ? "Part" : part.name.c_str(), nullptr,
                      part.type == InstrumentType::Bass ? "Edit this bass part      Right click  its hit sound, or take it out of the song"
                                                        : "Edit this guitar part      Right click  its hit sound, or take it out of the song", i == editor.part)) choosePart(i);
        if (ImGui::IsItemClicked(ImGuiMouseButton_Right)){
            choosePart(i);
            openPartMenu = true;
            partMenuAt = ImVec2(partX, bar.middle + BUTTON_HEIGHT * s / 2 + 4 * s);
        }
        ImGui::PopID();
    }
    if (openPartMenu){
        ImGui::OpenPopup(PART_POPUP);
        ImGui::SetNextWindowPos(partMenuAt);
    }
    pushCompactStyle(s);
    if (ImGui::BeginPopup(PART_POPUP)){
        // What a note hit in the game sounds like: the part's own instrument unless chosen otherwise; heard as it's chosen
        if (ImGui::BeginMenu("Hit sound")){
            const bool bass = track().type == InstrumentType::Bass;
            const char* labels[] = { "Bass", "Clean guitar", "Pluck", "Soft", "Keys", "Drop", "None" };
            auto choose = [&](const std::string& name, const char* label){
                if (!ImGui::MenuItem(label, nullptr, track().hitSound == name)) return;
                track().hitSound = name;
                markChanged();
                const float frequency = midiToFrequency(bass ? 40.0f : 52.0f);
                if (name.empty() || name == "bass" || name == "guitar") playStringNote(frequency, name.empty() ? bass : name == "bass", 0.6f, 1.0f);
                else if (name == "drop") playHitSound(true);
                else if (name != "none") playBuiltInNote(name.c_str(), frequency * 2.0f, 1.0f);
            };
            choose("", bass ? "Its instrument (bass)" : "Its instrument (clean guitar)");
            ImGui::Separator();
            for (int k = 0; k < (int)(sizeof HIT_SOUNDS / sizeof HIT_SOUNDS[0]); k++){
                if ((bass && k == 0) || (!bass && k == 1)) continue; // its own instrument is the first choice
                choose(HIT_SOUNDS[k], labels[k]);
            }
            ImGui::EndMenu();
        }
        ImGui::BeginDisabled(!severalParts); // a song keeps at least one part
        if (ImGui::Selectable(TextFormat("Take %s out of the song", track().name.empty() ? "this part" : track().name.c_str()))) removePart();
        ImGui::EndDisabled();
        ImGui::EndPopup();
    }
    popCompactStyle();
    const float addX = bar.x;
    if (barButton(bar, "##addpart", nullptr, "Add a guitar or a bass part", false, true, Icon::Plus)) ImGui::OpenPopup(ADD_PART_POPUP);
    if (barButton(bar, "##removepart", nullptr, severalParts ? "Take the part being edited out of the song (Ctrl + Z brings it back)" : "A song keeps at least one part",
                  false, severalParts, Icon::Minus)) removePart();
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
    if (barToggle(bar, "Names", "Each note's name in it, under its fret", editor.settings->editorNoteNames)){
        editor.settings->editorNoteNames = !editor.settings->editorNoteNames;
    }
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
    if (chart.videoFile.empty()){
        note("No video: Import brings one in");
    } else {
        note(TextFormat("Video: %s, behind the notes when the song is played", chart.videoFile.c_str()));
        field("Seconds into the video where the audio starts");
        if (ImGui::InputDouble("##videooffset", &chart.videoOffset, 0.05, 0.5, "%.2f")) markChangedInRun();
        if (ImGui::Button("Take the video off")){
            chart.videoFile.clear(); // its file stays in the folder: undoing brings it back
            chart.videoOffset = 0.0;
            markChanged();
        }
    }

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
    // The song's own beat, found from its sound; then, from what was found, the two things a guess can get wrong
    ImGui::BeginDisabled(!editor.songLoaded || findingTempo.working);
    if (ImGui::Button("Find the tempo from the song")) findTempo();
    ImGui::EndDisabled();
    if (editor.foundBeats.empty()){
        note(editor.songLoaded ? "Listens to the song for its beats and lays the bars on them: its tempo (following it if it drifts) and where bar 1 starts"
                               : "Needs the song's audio");
    } else {
        note("If the bars start on the wrong beat, or the tempo is half or twice the one you count:");
        ImGui::BeginDisabled(editor.foundFirst <= 0);
        if (ImGui::Button("Bar 1 a beat earlier")) layBarsOnBeats(editor.foundFirst - 1);
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("a beat later")) layBarsOnBeats(editor.foundFirst + 1);
        if (ImGui::Button("Half the tempo")) halveBeats();
        ImGui::SameLine();
        if (ImGui::Button("Twice the tempo")) doubleBeats();
    }
    field("Offset: where the first bar starts in the audio (s)");
    if (ImGui::InputDouble("##offset", &chart.offset, 0.001, 0.01, "%.3f")) markChanged();
    if (editor.songLoaded){
        // Trimmed: the part of the audio that's the song. The file itself is left whole.
        const double length = audioSeconds();
        field("The song starts at (s into the audio)");
        double start = chart.trimStart;
        if (ImGui::InputDouble("##trimstart", &start, 0.1, 1.0, "%.3f")){
            chart.trimStart = std::clamp(start, 0.0, std::max(0.0, songEndSeconds() - MIN_TRIMMED_S));
            markChanged();
        }
        field(TextFormat("and ends at (0: the audio's end, %.3f)", length));
        double end = chart.trimEnd;
        if (ImGui::InputDouble("##trimend", &end, 0.1, 1.0, "%.3f")){
            end = end <= 0.0 || end >= length ? 0.0 : std::max(end, chart.trimStart + MIN_TRIMMED_S);
            chart.trimEnd = end >= length ? 0.0 : end;
            markChanged();
        }
    }
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
        case Drag::Slide:
            if (!editor.dragMoved) movePlayhead(snapTick(editor.dragFromTick)); // a click on the waveform: the playhead goes there
            else if (editor.dragChanged) restartPlayback();
            break;
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
    const bool names = editor.settings->editorNoteNames;
    const float radius = std::min((names ? NAMED_NOTE_RADIUS : NOTE_RADIUS) * s, rowHeight * 0.4f);
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
    const bool overRuler = hovered && mouse.x >= gridLeft && mouse.y < rulerBottom;
    const bool overWave = hovered && mouse.x >= gridLeft && mouse.y >= rulerBottom && mouse.y < rowsTop;
    editor.hoverString = overRows ? stringToRow(mouseRow) : -1;
    // The song's two edges in its audio, on the waveform: taken hold of to trim it
    const float trimStartX = tickToX(secondsToTick(chart, chart.trimStart)), trimEndX = tickToX(secondsToTick(chart, songEndSeconds()));
    const bool overTrimStart = overWave && std::fabs(mouse.x - trimStartX) <= TRIM_GRIP * s;
    const bool overTrimEnd = overWave && !overTrimStart && std::fabs(mouse.x - trimEndX) <= TRIM_GRIP * s;

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
        if (overRuler){
            editor.drag = Drag::Playhead;
            editor.resumeAfterDrag = editor.playing;
            stopPlayback();
        } else if (overWave){
            editor.drag = overTrimStart ? Drag::TrimStart : overTrimEnd ? Drag::TrimEnd : Drag::Slide;
            editor.slideFromOffset = chart.offset;
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
        if (editor.drag != Drag::Pan && editor.drag != Drag::Erase && editor.drag != Drag::Slide && editor.dragMoved){
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
            case Drag::Slide: {
                // The waveform follows the mouse: the audio that was under it when the button went down stays
                // under it. With Shift, ten times finer.
                if (!editor.dragMoved) break;
                double moved = songSeconds(mouseTick) - songSeconds(editor.dragFromTick);
                if (io.KeyShift) moved /= 10.0;
                double offset = std::round((editor.slideFromOffset - moved) * 10000.0) / 10000.0;
                if (offset != chart.offset){
                    editor.chart.offset = offset;
                    editor.dragChanged = true;
                    markChanged();
                }
                break;
            }
            case Drag::TrimStart:
            case Drag::TrimEnd: {
                // The edge follows the mouse, taking to the grid when it's close (not with Alt)
                if (!editor.dragMoved) break;
                double tick = mouseTick;
                if (!io.KeyAlt && std::fabs(tickToX(snapTick(mouseTick)) - mouse.x) < TRIM_GRIP * s) tick = snapTick(mouseTick);
                double seconds = std::round(songSeconds(tick) * 1000.0) / 1000.0, length = audioSeconds();
                double& edge = editor.drag == Drag::TrimStart ? editor.chart.trimStart : editor.chart.trimEnd;
                double to;
                if (editor.drag == Drag::TrimStart) to = std::clamp(seconds, 0.0, std::max(0.0, songEndSeconds() - MIN_TRIMMED_S));
                else {
                    to = std::clamp(seconds, std::min(length, chart.trimStart + MIN_TRIMMED_S), length);
                    if (to >= length - 0.005) to = 0.0; // back at the audio's end: not trimmed
                }
                if (to != edge){
                    edge = to;
                    markChanged();
                }
                break;
            }
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

    // What's trimmed away: faded, with the song's two edges on the waveform to take hold of
    if (wave){
        const ImU32 away = uiColor(UiColor::Background, 0.62f);
        if (chart.trimStart > 0.0 && trimStartX > gridLeft) draw->AddRectFilled(ImVec2(gridLeft, rulerBottom + 1.0f), ImVec2(trimStartX, rowsBottom), away);
        if (chart.trimEnd > 0.0 && trimEndX < gridRight) draw->AddRectFilled(ImVec2(std::max(trimEndX, gridLeft), rulerBottom + 1.0f), ImVec2(gridRight, rowsBottom), away);
        auto edge = [&](float x, bool start, bool lit){
            const ImU32 color = lit ? uiColor(UiColor::Accent) : uiColor(UiColor::Ink, 0.75f);
            const float inward = start ? 1.0f : -1.0f, top = rulerBottom + 3 * s, bottom = rowsTop - 3 * s;
            verticalLine(draw, x, top, bottom, 2.0f * s, color);
            // A bracket's two feet, turned towards the song
            draw->AddRectFilled(ImVec2(x, top), ImVec2(x + inward * 8 * s, top + 3 * s), color);
            draw->AddRectFilled(ImVec2(x, bottom - 3 * s), ImVec2(x + inward * 8 * s, bottom), color);
        };
        edge(trimStartX, true, overTrimStart || editor.drag == Drag::TrimStart);
        edge(trimEndX, false, overTrimEnd || editor.drag == Drag::TrimEnd);
    }

    // In a note: its fret, or with the names on, its fret over the note it is (the neck learned while charting)
    auto label = [&](ImVec2 center, int stringIndex, int fret, ImU32 color){
        const char* number = TextFormat("%d", fret);
        if (!names){
            const float size = radius * 1.05f;
            draw->AddText(fonts.bold, size, ImVec2(center.x - textWidth(fonts.bold, size, number) / 2, center.y - size / 2 - s), color, number);
            return;
        }
        const char* name = pitchClassName(track().tuning[stringIndex] + fret);
        const float size = radius * 0.88f, nameSize = radius * 0.6f, top = center.y - (size + nameSize * 0.85f) / 2 - s;
        draw->AddText(fonts.bold, size, ImVec2(center.x - textWidth(fonts.bold, size, number) / 2, top), color, number);
        draw->AddText(fonts.bold, nameSize, ImVec2(center.x - textWidth(fonts.bold, nameSize, name) / 2, top + size * 0.92f),
                      (color & IM_COL32(255, 255, 255, 0)) | IM_COL32(0, 0, 0, 215), name);
    };
    // Where a new note would go: its place on the grid marked up through the waveform, the note itself faint, with
    // its fret and its name
    const bool ghost = overRows && !overNote && !overGrip && editor.drag == Drag::None && !io.KeyShift;
    if (ghost){
        int tick = snapTick(xToTick(mouse.x)), pitch = track().tuning[editor.hoverString] + editor.newNoteFret;
        ImVec2 center(tickToX(tick), rowY(editor.hoverString));
        verticalLine(draw, center.x, rulerBottom, rowsBottom, 1.0f, uiColor(UiColor::Accent, 0.55f));
        draw->AddCircleFilled(center, radius, stringInk(editor.hoverString, 0.28f));
        draw->AddCircle(center, radius, stringInk(editor.hoverString), 0, 1.5f * s);
        label(center, editor.hoverString, editor.newNoteFret, uiColor(UiColor::Ink));
        if (!names) draw->AddText(fonts.mono, 12 * s, ImVec2(center.x + radius + 5 * s, center.y - radius - 4 * s), uiColor(UiColor::Ink), pitchText(pitch));
        hoverTip = TextFormat("Click  place fret %d (%s)      Wheel  another fret      Drag right  hold it      Shift + drag  select      Shift + wheel  scroll",
                              editor.newNoteFret, pitchText(pitch));
    }

    // The playhead's line goes under the notes, so one sitting on it can still be read
    const float playheadX = tickToX(playhead);
    const ImU32 accent = uiColor(editor.recording ? UiColor::Bad : UiColor::Accent); // red while it records
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
        // As in a song: dark glass edged in the string's neon (by day, tinted glass), the selected ones lit
        const Color color = stringColor(note.stringIndex), card = themeColor(UiColor::Card);
        const bool night = currentTheme() == ThemeMode::Dark;
        const Color fill = selected ? ColorLerp(card, color, 0.45f) : ColorLerp(card, color, night ? 0.16f : 0.14f);
        const Color ink = night ? ColorLerp(color, WHITE, selected ? 0.75f : 0.35f) : ColorLerp(color, BLACK, 0.2f);
        draw->AddCircleFilled(center, radius + 1.5f * s, uiColor(UiColor::Card)); // a rim, so notes side by side stay apart
        draw->AddCircleFilled(center, radius, IM_COL32(fill.r, fill.g, fill.b, 255));
        draw->AddCircle(center, radius - 1.0f * s, stringInk(note.stringIndex), 0, 2.0f * s);
        if (selected) draw->AddCircle(center, radius + 3.5f * s, uiColor(UiColor::Accent), 0, 2.5f * s);
        else if (under) draw->AddCircle(center, radius + 3.0f * s, uiColor(UiColor::Ink, 0.5f), 0, 1.5f * s);
        label(center, note.stringIndex, note.fret, IM_COL32(ink.r, ink.g, ink.b, 255));
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

    if (overRuler && editor.drag == Drag::None) hoverTip = "Click or drag  move the playhead      Wheel  along the song      Ctrl + wheel  zoom";
    const bool trimming = editor.drag == Drag::TrimStart || editor.drag == Drag::TrimEnd;
    if (editor.drag == Drag::Slide && editor.dragMoved){
        hoverTip = TextFormat("Offset %.3f s  where the first bar starts in the audio      Shift  ten times finer", chart.offset);
    } else if (trimming || ((overTrimStart || overTrimEnd) && editor.drag == Drag::None)){
        const bool start = editor.drag == Drag::TrimStart || (editor.drag == Drag::None && overTrimStart);
        if (start) hoverTip = TextFormat("Drag  where the song starts: %.2f s into its audio      Alt  off the grid", chart.trimStart);
        else hoverTip = TextFormat("Drag  where the song ends: %.2f s into its audio%s      Alt  off the grid", songEndSeconds(), chart.trimEnd > 0.0 ? "" : " (its end)");
    } else if (overWave && editor.drag == Drag::None){
        hoverTip = "Drag  slide the song under the grid      Drag an edge  trim the song      Click  move the playhead      Alt + Left Right  slide by 1 ms";
    }
    if (overGrip || editor.drag == Drag::Length || trimming || ((overTrimStart || overTrimEnd) && editor.drag == Drag::None)) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
    else if (editor.drag == Drag::Slide && editor.dragMoved) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
    else if (overNote) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
}

// The whole song, small: its waveform, its notes, the part on screen (dragged, it moves the view) and the playhead
static void drawOverview(ImVec2 min, ImVec2 max, float s){
    Chart& chart = editor.chart;
    const int strings = (int)track().tuning.size();
    const float width = max.x - min.x, height = max.y - min.y;
    // All of the song: its bars, and its audio if that goes on past them
    const double total = std::max({ 1.0, (double)chart.endTick, editor.songLoaded ? secondsToTick(chart, audioSeconds()) : 0.0 });
    auto tickAt = [&](float x){ return (double)(x - min.x) / width * total; };
    auto xAt = [&](double tick){ return min.x + (float)(tick / total) * width; };

    // Its trim's edges can be dragged here too, where the whole song is in sight (a bar line takes them when close)
    const float trimStartX = xAt(secondsToTick(chart, chart.trimStart)), trimEndX = std::min(max.x, xAt(secondsToTick(chart, songEndSeconds())));
    static int trimming = 0; // 0: the view follows the mouse; 1 the start, 2 the end being dragged
    const float mouseX = ImGui::GetIO().MousePos.x;
    const bool overStart = editor.songLoaded && std::fabs(mouseX - trimStartX) <= TRIM_GRIP * s;
    const bool overEnd = editor.songLoaded && !overStart && std::fabs(mouseX - trimEndX) <= TRIM_GRIP * s;

    ImGui::SetCursorScreenPos(min);
    ImGui::InvisibleButton("overview", ImVec2(width, height));
    if (ImGui::IsItemHovered()){
        if (overStart || overEnd || trimming) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
        hoverTip = overStart ? "Drag  where the song starts in its audio"
                 : overEnd ? "Drag  where the song ends in its audio"
                 : "The whole song: click or drag to go there      Drag its faded ends' edges  trim it";
    }
    if (ImGui::IsItemActivated()) trimming = overStart ? 1 : overEnd ? 2 : 0;
    if (ImGui::IsItemActive()){
        if (trimming == 0){
            editor.viewStartTick = tickAt(mouseX) - visibleTicks() / 2;
            clampView();
        } else {
            double tick = std::clamp(tickAt(mouseX), 0.0, total);
            int bar = barNumberAt(chart, std::max(0, (int)std::lround(tick)));
            for (int line : { barStartTick(chart, bar), barStartTick(chart, bar + 1) }){
                if (std::fabs(xAt(line) - mouseX) < TRIM_GRIP * s && !ImGui::GetIO().KeyAlt) tick = line;
            }
            double seconds = std::round(songSeconds(tick) * 1000.0) / 1000.0, length = audioSeconds(), to;
            double& edge = trimming == 1 ? chart.trimStart : chart.trimEnd;
            if (trimming == 1) to = std::clamp(seconds, 0.0, std::max(0.0, songEndSeconds() - MIN_TRIMMED_S));
            else {
                to = std::clamp(seconds, std::min(length, chart.trimStart + MIN_TRIMMED_S), length);
                if (to >= length - 0.005) to = 0.0; // back at the audio's end: not trimmed
            }
            if (to != edge){
                edge = to;
                markChanged();
            }
            hoverTip = TextFormat("The song %s at %.2f s into its audio      Alt  off the bar lines", trimming == 1 ? "starts" : "ends",
                                  trimming == 1 ? chart.trimStart : songEndSeconds());
        }
    } else {
        trimming = 0;
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
    if (editor.songLoaded){ // what's trimmed away, faded
        const ImU32 away = uiColor(UiColor::Background, 0.62f);
        if (chart.trimStart > 0.0) draw->AddRectFilled(min, ImVec2(xAt(secondsToTick(chart, chart.trimStart)), max.y), away);
        if (chart.trimEnd > 0.0) draw->AddRectFilled(ImVec2(xAt(secondsToTick(chart, chart.trimEnd)), min.y), max, away);
    }
    float from = xAt(std::max(0.0, editor.viewStartTick)), to = std::max(from + 4 * s, xAt(std::min(total, editor.viewStartTick + visibleTicks())));
    draw->AddRectFilled(ImVec2(from, min.y), ImVec2(to, max.y), uiColor(UiColor::Accent, 0.14f), 4 * s);
    draw->AddRect(ImVec2(from, min.y), ImVec2(to, max.y), uiColor(UiColor::Accent), 4 * s, 0, 1.5f * s);
    int playhead = editor.playing ? (int)secondsToTick(chart, playbackSeconds()) : editor.playheadTick;
    verticalLine(draw, xAt(playhead), min.y, max.y, 2.0f * s, uiColor(UiColor::Accent));
    if (editor.songLoaded){
        // The trim's edges, with a grip each
        for (int edge = 1; edge <= 2; edge++){
            const float x = edge == 1 ? trimStartX : trimEndX;
            const bool lit = trimming == edge || (trimming == 0 && (edge == 1 ? overStart : overEnd));
            const ImU32 color = uiColor(lit ? UiColor::Accent : UiColor::Ink, lit ? 1.0f : 0.6f);
            verticalLine(draw, x, min.y, max.y, 2.0f * s, color);
            const float gripX = edge == 1 ? x : x - 5 * s;
            draw->AddRectFilled(ImVec2(gripX, min.y + height / 2 - 7 * s), ImVec2(gripX + 5 * s, min.y + height / 2 + 7 * s), color, 2 * s);
        }
    }
    draw->PopClipRect();
}

// --- Keys ---------------------------------------------------------------------------------------------

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
        if (editor.playing) stopPlayback(); // a take under way ends with it
        else startPlayback();
    }
    if (!ctrl && ImGui::IsKeyPressed(ImGuiKey_R, false)){
        if (editor.recording) stopRecording();
        else startRecording();
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
    // With Alt: the song itself, slid a millisecond under the grid (ten with Shift), the way the waveform moves.
    int along = (ImGui::IsKeyPressed(ImGuiKey_RightArrow) ? 1 : 0) - (ImGui::IsKeyPressed(ImGuiKey_LeftArrow) ? 1 : 0);
    if (along != 0 && io.KeyAlt){
        if (editor.songLoaded) slideSong(-along * (io.KeyShift ? 0.010 : 0.001));
    } else if (along != 0 && ctrl){
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
        { "Ruler", "click or drag to move the playhead (a click on the waveform too)" },
        { "Drag the waveform", "slide the song under the grid; with Shift, finer" },
        { "Drag its edges", "trim where the song starts and ends" },
    };
    static const Row KEYS[] = {
        { "0 - 9", "type a fret: 1 then 2 is 12" },
        { "Up  Down", "fret up or down" },
        { "Ctrl + Up  Down", "the same notes on the next string" },
        { "Left  Right", "move the selected notes; with none, the playhead" },
        { "Shift + Left  Right", "held shorter or longer" },
        { "Ctrl + Left  Right", "the playhead, bar by bar" },
        { "Alt + Left  Right", "slide the song by 1 ms; with Shift, by 10" },
        { "Delete", "delete the selected notes" },
        { "Ctrl + A  C  X  V", "select all, copy, cut, paste at the playhead" },
        { "Ctrl + Z  Y", "undo, redo" },
        { "Space   Home  End", "play or stop; the playhead to the start, the end" },
        { "R", "record: play the part on your instrument, over the song" },
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
        column("MOUSE", MOUSE, (int)(sizeof(MOUSE) / sizeof(MOUSE[0])), 160);
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

// --- Import and export, asked -------------------------------------------------------------------------

// A choice in a popup: a button, and under it what it does
static bool choiceButton(const char* label, const char* explanation, float width){
    bool pressed = ImGui::Button(label, ImVec2(width, 0));
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + width);
    ImGui::TextDisabled("%s", explanation);
    ImGui::PopTextWrapPos();
    ImGui::Spacing();
    return pressed;
}

static void drawImportExportPopups(float s){
    if (editor.openImport){
        ImGui::OpenPopup(IMPORT_POPUP);
        editor.importVideoSound = editor.chart.audioFile.empty(); // a song with no audio yet most likely wants it
    }
    if (editor.openExport) ImGui::OpenPopup(EXPORT_POPUP);
    editor.openImport = editor.openExport = false;
    const float width = 420 * s;
    const ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    pushCompactStyle(s);

    ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal(IMPORT_POPUP, nullptr, ImGuiWindowFlags_AlwaysAutoResize)){
        std::string path, error;
        if (choiceButton("Parts, from a tab or another song...", "A Guitar Pro tab (.gp, .gpx, .gp3 to .gp5) or a lahn chart (.chart): you choose which of its parts come in, and whether its bars and tempos do.", width)){
            if (chooseFile("Choose a tab or a chart", "Tabs and charts", PARTS_PATTERNS, path, error)) readPartsFile(path);
            else if (!error.empty()) editor.status = error;
            ImGui::CloseCurrentPopup();
        }
        if (choiceButton("Its audio...", "An mp3, ogg, flac or wav file: copied into the song, in place of the audio it has.", width)){
            if (chooseFile("Choose the song's audio", "Audio", AUDIO_PATTERNS, path, error)) importAudio(path);
            else if (!error.empty()) editor.status = error;
            ImGui::CloseCurrentPopup();
        }
        if (choiceButton("Its video...", "A video of any kind (mp4, mkv, webm, mov...): shown behind the notes while the song is played. Kinds other than .mpg are converted, which needs lahn's video add-on.", width)){
            if (chooseFile("Choose the song's video", "Videos", VIDEO_PATTERNS, path, error)) importVideo(path, editor.importVideoSound);
            else if (!error.empty()) editor.status = error;
            ImGui::CloseCurrentPopup();
        }
        ImGui::Checkbox("and its sound, as the song's audio", &editor.importVideoSound);
        ImGui::Spacing();
        ImGui::TextDisabled("Or drop the file on the editor.");
        ImGui::Spacing();
        if (ImGui::Button("Close") || ImGui::IsKeyPressed(ImGuiKey_Escape, false)) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    if (editor.openImportParts) ImGui::OpenPopup(IMPORT_PARTS_POPUP);
    editor.openImportParts = false;
    ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal(IMPORT_PARTS_POPUP, nullptr, ImGuiWindowFlags_AlwaysAutoResize)){
        ImGui::Text("%s", editor.importName.c_str());
        ImGui::Spacing();
        int chosen = 0;
        for (int i = 0; i < (int)editor.importChart.frettedTracks.size(); i++){
            const FrettedTrack& part = editor.importChart.frettedTracks[i];
            bool on = editor.importChosen[i] != 0;
            ImGui::PushID(i);
            if (ImGui::Checkbox(TextFormat("%s   %s, %d strings, %d notes", part.name.empty() ? "Part" : part.name.c_str(),
                                           part.type == InstrumentType::Bass ? "bass" : "guitar", (int)part.tuning.size(), (int)part.notes.size()), &on)){
                editor.importChosen[i] = on;
            }
            ImGui::PopID();
            chosen += on;
        }
        ImGui::Spacing();
        // What the file says of its music's count: the song takes it, unless this is unticked
        const Chart& from = editor.importChart;
        std::string counted = from.tempoMap.empty() ? "" : TextFormat("%.5g BPM", from.tempoMap[0].bpm);
        if (!from.timeSignatures.empty()) counted += TextFormat("%s%d/%d", counted.empty() ? "" : ", ", from.timeSignatures[0].beats, from.timeSignatures[0].beatUnit);
        int changes = (int)from.tempoMap.size() + (int)from.timeSignatures.size() - 2;
        if (changes > 0) counted += TextFormat(", %d %s along the way", changes, changes == 1 ? "change" : "changes");
        ImGui::Checkbox(TextFormat("The song takes its tempo and bars: %s", counted.c_str()), &editor.importBars);
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + width);
        ImGui::TextDisabled("%s", editor.importBars ? "Its tempos, time signatures and key replace this song's own, and the song's length in bars follows. Notes already here stay on their bars and beats."
                                                    : "The song keeps its own tempo and bars, and the parts are laid on them: bar 5, beat 2 stays bar 5, beat 2.");
        if (!editor.importLeftOut.empty()){
            std::string left = "Not brought in: ";
            for (size_t i = 0; i < editor.importLeftOut.size(); i++) left += (i ? ", " : "") + editor.importLeftOut[i];
            ImGui::TextDisabled("%s", left.c_str());
        }
        ImGui::PopTextWrapPos();
        ImGui::Spacing();
        ImGui::BeginDisabled(chosen == 0);
        if (ImGui::Button(TextFormat(chosen == 1 ? "Bring in %d part" : "Bring in %d parts", chosen))){
            bringPartsIn();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape, false)) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal(EXPORT_POPUP, nullptr, ImGuiWindowFlags_AlwaysAutoResize)){
        const bool hasVideo = !editor.chart.videoFile.empty();
        if (choiceButton("The whole song, one file (.lahn)", hasVideo ? "Its chart, its audio and, if you leave it ticked, its video. Anyone with lahn installs it by dropping it on their song list."
                                                                     : "Its chart and its audio. Anyone with lahn installs it by dropping it on their song list.", width)){
            exportPackage();
            ImGui::CloseCurrentPopup();
        }
        if (hasVideo){
            ImGui::Checkbox("with its video (a much bigger file)", &editor.exportVideo);
            ImGui::Spacing();
        }
        if (choiceButton("Its chart alone (.chart)", "The parts, the bars and the tempos, without the audio: for someone who has the recording. They bring it into their own song with Import.", width)){
            exportChart();
            ImGui::CloseCurrentPopup();
        }
        if (ImGui::Button("Close") || ImGui::IsKeyPressed(ImGuiKey_Escape, false)) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    // The song being listened to for its beat
    takeTempoWork();
    if (findingTempo.working && !ImGui::IsPopupOpen(TEMPO_POPUP)) ImGui::OpenPopup(TEMPO_POPUP);
    ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal(TEMPO_POPUP, nullptr, ImGuiWindowFlags_AlwaysAutoResize)){
        ImGui::Text("Finding its tempo and its beats...");
        ImGui::TextDisabled("A second or two for a whole song.");
        ImGui::Spacing();
        if (ImGui::Button("Stop")) findingTempo.cancel = true;
        if (!findingTempo.working) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    // A video being converted: how far along, and a way out
    takeVideoWork();
    if (bringingVideo.working && !ImGui::IsPopupOpen(VIDEO_POPUP)) ImGui::OpenPopup(VIDEO_POPUP);
    ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal(VIDEO_POPUP, nullptr, ImGuiWindowFlags_AlwaysAutoResize)){
        ImGui::Text("%s", bringingVideo.file.c_str());
        ImGui::TextDisabled("%s", bringingVideo.stage == 0 ? "Its sound, into the song's audio" : "Its pictures, into a video lahn plays");
        ImGui::ProgressBar(bringingVideo.progress.load(), ImVec2(width, 0));
        ImGui::Spacing();
        if (ImGui::Button("Stop")) bringingVideo.cancel = true;
        if (!bringingVideo.working) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    popCompactStyle();
}

// --- Screen -------------------------------------------------------------------------------------------

bool openEditor(const SongEntry& song, const std::string& userSongsDir, const std::string& packagesDir, const std::string& addonsDir,
                Settings& settings, std::string& error){
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
    fresh.addonsDir = addonsDir;
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
    stopTempoWork();
    stopVideoWork();
    stopRecording();
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
    drawBottomBar(s, width, height, "Space  play      R  record      Wheel on a string  the fret of the next note      Shift + wheel  along the song      Ctrl + wheel  zoom      F1  every key");
    drawKeysPopup(s);
    drawImportExportPopups(s);
    // A file dropped on the editor is brought into the song
    if (IsFileDropped()){
        FilePathList dropped = LoadDroppedFiles();
        if (dropped.count > 0 && !popupOpen) importFile(dropped.paths[0]);
        UnloadDroppedFiles(dropped);
    }

    // Nothing held, and no run of wheel notches or key repeats under way: what changed is one undo step
    // (a take is one step too: nothing is committed while it's being played)
    if (!ImGui::IsAnyItemActive() && GetTime() >= editor.settleUntil && !editor.recording) commitChange();
    if (editor.playing){
        schedulePlayback();
        // Past the end of both the chart and the song, playback stops on its own
        double end = std::max(tickToSeconds(editor.chart, editor.chart.endTick), editor.songLoaded ? songLength() : 0.0);
        if (playbackSeconds() > end + 0.5) stopPlayback();
    }
    if (editor.recording){
        if (editor.playing) recordPlayed();
        else stopRecording(); // the song stopped, or was stopped
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
            stopRecording();
            stopPlayback();
            setSongVolume(1.0f); // the game plays it as it is
            choice = EditorChoice::TestPlay;
        }
    }
    if (requestBack){
        stopRecording();
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

void editorImportFile(const std::string& path){
    if (editor.active) importFile(path);
}

void editorFindTempo(){
    if (editor.active) findTempo();
}
