#pragma once

#include <string>

// Where the player's own files live: settings, songs, exercises, lessons, progress.
// The game's install folder may not be writable (e.g. Program Files), so these go in the OS's per-user data folder:
//   Windows: %APPDATA%\lahn
//   macOS:   ~/Library/Application Support/lahn
//   Linux:   $XDG_DATA_HOME/lahn, or ~/.local/share/lahn
std::string userDataDir();

// The same place under the game's first name, OpenMusicTrainer: where data from before the rename is
std::string oldUserDataDir();

// Moves a data folder to its new name, once: only if `from` exists and `to` doesn't, so it never overwrites
// anything. False (with a reason) if the move failed; nothing to move is not a failure.
bool moveUserDataFolder(const std::string& from, const std::string& to, std::string& error);
