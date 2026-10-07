#pragma once

#include <string>
#include <vector>

// Where the player's own files live: settings, songs, exercises, lessons, progress.
// The game's install folder may not be writable (e.g. Program Files), so these go in the OS's per-user data folder:
//   Windows: %APPDATA%\hardthz
//   macOS:   ~/Library/Application Support/hardthz
//   Linux:   $XDG_DATA_HOME/hardthz, or ~/.local/share/hardthz
std::string userDataDir();

// The same place under the game's earlier names (lahn, and first OpenMusicTrainer), the latest first: where data from
// before a rename is
std::vector<std::string> oldUserDataDirs();

// Moves a data folder to its new name, once: only if `from` exists and `to` doesn't, so it never overwrites
// anything. False (with a reason) if the move failed; nothing to move is not a failure.
bool moveUserDataFolder(const std::string& from, const std::string& to, std::string& error);
