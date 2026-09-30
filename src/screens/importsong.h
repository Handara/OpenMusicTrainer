#pragma once

#include <string>

// Importing a song: a Guitar Pro tab, chosen with the system's dialog or dropped on the window. Its guitar and bass
// parts are shown, with what couldn't come in; then its audio: the song's own recording (lined up afterwards in the
// song editor), or lahn's backing, every part on its synths with a click, in time from the start.

void openImportScreen(const std::string& songsDir, const std::string& file); // `file`: a tab already chosen, or ""
enum class ImportChoice { None, Imported };
ImportChoice importScreen();
std::string importedSongTitle(); // after Imported: what it's called
void closeImportScreen();
