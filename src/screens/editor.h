#pragma once

#include "core/chart.h"
#include "core/settings.h"
#include "core/songlibrary.h"

#include <string>

// The chart editor: a timeline of the song, a row per string, where notes are placed, moved, held and re-fretted with
// the mouse (the wheel changes a fret) or the keyboard; the song's details in a drawer; play, record, test play, save, import and export.
// Built-in songs are read-only; saving one creates an editable copy in the user's songs folder.

// packagesDir: where Export puts the song's package (.lahn). `settings`: the string order, and the editor's own
// volumes, which it changes (they're saved with the rest); it must outlive the editor.
// addonsDir: where add-ons are installed (the video add-on, for bringing videos in).
bool openEditor(const SongEntry& song, const std::string& userSongsDir, const std::string& packagesDir, const std::string& addonsDir,
                Settings& settings, std::string& error);
// A file brought into the song being edited, as if dropped on it: a tab or a chart (its parts), an audio file, a video
void editorImportFile(const std::string& path);
// The song being edited listened to for its tempo and beats, its bars laid on them (as its Details' button does)
void editorFindTempo();

enum class EditorChoice { None, Back, TestPlay };
EditorChoice editorScreen(); // full-screen ImGui; handles its own Esc so it can warn about unsaved changes
void closeEditor();          // safe to call more than once

// Test-play (F5): the chart as it is now, saved or not, played from the playhead. The editor stays open meanwhile;
// resumeEditor brings it back (gameplay took the song's audio, so it's loaded again), with a message for its status
// line if there is one (why the test couldn't start).
struct EditorTestPlay {
    Chart chart;
    std::string audioPath;
    int fromTick;
    int part;       // the part being edited: the one played
};
EditorTestPlay editorTestPlay();
void resumeEditor(const std::string& message = "");
