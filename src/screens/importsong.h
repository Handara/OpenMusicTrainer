#pragma once

#include <string>

// Importing a song, chosen with the system's dialog or dropped on the window, from one of three things:
// - a Guitar Pro tab: its guitar and bass parts are shown, with what couldn't come in; then its audio: the song's own
//   recording (lined up afterwards in the song editor), or lahn's backing, every part on its synths with a click;
// - a recording of a bass alone: its line written down (core/transcribe), in the background;
// - any song: its bass taken out of it first, by the stems add-on (core/stemsplit, app/stemmodel), installed from here
//   too; played along to whole, without its bass, or as its bass alone.

// `file`: a tab, a song or the stems add-on already chosen, or "". addonsDir: where add-ons are installed.
void openImportScreen(const std::string& songsDir, const std::string& addonsDir, const std::string& file);
enum class ImportChoice { None, Imported };
ImportChoice importScreen();
std::string importedSongTitle(); // after Imported: what it's called
void closeImportScreen();
