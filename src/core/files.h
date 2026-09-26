#pragma once

#include <string>

// Replaces a file's contents safely: writes a temporary file next to it, then swaps it in with one rename.
// If the program crashes or the disk fills up midway, the old file is still intact.
bool writeFileAtomically(const std::string& path, const std::string& content, std::string& error);
