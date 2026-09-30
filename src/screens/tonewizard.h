#pragma once

#include "core/settings.h"
#include "core/tonechain.h"

#include <string>
#include <vector>

// The player's tones, for the whole app: read once from their tones folder (core/tonelibrary)
void initTones(const std::string& tonesFolder);
Tone toneNamed(const std::string& name);  // the player's or built in; Clean if there's none of that name
std::vector<std::string> toneNames();     // built in first, then the player's
// The instrument's own sound heard through the settings' tone, at the settings' volume (audio.h: setMonitorTone)
void applyTone(const Settings& settings);

// The tone wizard: the chain of effects the instrument's sound goes through, laid out like pedals on a board, each
// with its knobs. Played while it's changed, every change heard at once. A built-in tone changed becomes the player's
// own copy; the player's tones save themselves. Tones are files: shared from the tones folder, and added by dropping
// one on the window.
void openToneWizard(Settings& settings);
void toneWizardScreen(Settings& settings);
void closeToneWizard(Settings& settings); // saves anything not saved yet
