#pragma once

#include "core/settings.h"

#include <string>

// The settings screen. Changes apply immediately (hear the new sound, switch device right away);
// the caller saves the settings when the player leaves.

void applyDisplaySettings(const Settings& settings); // fullscreen and frame rate: at startup, and when changed
// Hearing the instrument (audio.h: setMonitor): its inputs (the guitar's and bass's, never the voice's) and its amp.
// At startup, and whenever what it depends on changes. A problem opening the input is in `error`.
void applyMonitor(const Settings& settings, std::string& error);
void openSettingsScreen(const std::string& soundsDir); // refreshes the device and sound lists
void closeSettingsScreen();                            // lets go of the MIDI device it listens to
bool settingsUsedEscape(); // Esc this frame went to the screen itself (cancelling a key being chosen), not to leaving
enum class SettingsChoice { None, Back, CalibrateTapping, CalibrateInstrument };
// error: a problem from outside the screen to show (e.g. calibration couldn't open the input device)
SettingsChoice settingsScreen(Settings& settings, const std::string& soundsDir, const std::string& error);
