#pragma once

#include <string>

// Where the player's own files live: songs they made or downloaded (later also settings and scores).
// The game's install folder may not be writable (e.g. Program Files), so these go in the OS's per-user data folder:
//   Windows: %APPDATA%\OpenMusicTrainer
//   macOS:   ~/Library/Application Support/OpenMusicTrainer
//   Linux:   $XDG_DATA_HOME/OpenMusicTrainer, or ~/.local/share/OpenMusicTrainer
std::string userDataDir();
