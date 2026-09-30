#pragma once

#include <string>
#include <vector>

// The system's own "Open" dialog, for choosing a file: Windows' on Windows; on Linux zenity's or kdialog's, when one
// is installed. It holds the game until the player has chosen. `patterns` are like "*.gp". True with the file chosen;
// false when the player cancelled, or there's no dialog to show (then `error` says so: files can be dropped instead).
bool chooseFile(const std::string& title, const std::string& kind, const std::vector<std::string>& patterns,
                std::string& path, std::string& error);
