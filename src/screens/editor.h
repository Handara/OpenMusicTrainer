#pragma once

#include "core/songlibrary.h"

#include <string>

// The chart editor: place notes on a beat grid, edit the song's details, save.
// Built-in songs are read-only; saving one creates an editable copy in the user's songs folder.

bool openEditor(const SongEntry& song, const std::string& userSongsDir, bool lowStringOnTop, std::string& error);

enum class EditorChoice { None, Back };
EditorChoice editorScreen(); // full-screen ImGui; handles its own Esc so it can warn about unsaved changes
void closeEditor();          // safe to call more than once
