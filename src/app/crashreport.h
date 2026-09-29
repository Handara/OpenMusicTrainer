#pragma once

#include <string>

// When the game crashes: where it was (the call stack, with files and lines when the build has them) goes to the
// console and to a file, so a crash can be reported and fixed instead of just vanishing. Windows reads its own
// debug information for the names; Linux prints what the executable exports (addr2line turns the rest into lines).
void installCrashReport(const std::string& reportPath);
