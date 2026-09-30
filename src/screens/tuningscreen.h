#pragma once

#include "core/inputs.h"

#include <string>
#include <vector>

// Before a song on a guitar or a bass: the player plays each open string of the part's tuning, and each one shows
// how far off it is until it's in tune. Once every string is, the song starts; Enter skips it. Also where the play
// screen sends a player whose instrument went out of tune mid-song, with why (`reason`).

bool openTuningScreen(const std::vector<int>& tuning, InputRole instrument, const std::string& inputDevice, int channel,
                      const std::string& reason, std::string& error);
enum class TuningChoice { None, Tuned, Skipped };
TuningChoice tuningScreen(); // listens, draws, and says when the player is done
void closeTuningScreen();    // safe to call more than once
