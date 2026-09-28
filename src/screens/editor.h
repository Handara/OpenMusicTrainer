#pragma once

#include "core/chart.h"
#include "core/songlibrary.h"

#include <string>

// The chart editor: place notes on a beat grid, edit the song's details, save.
// Built-in songs are read-only; saving one creates an editable copy in the user's songs folder.

bool openEditor(const SongEntry& song, const std::string& userSongsDir, bool lowStringOnTop, std::string& error);

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
