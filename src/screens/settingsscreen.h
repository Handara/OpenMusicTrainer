#pragma once

#include "core/settings.h"

#include <string>

// The settings screen. Changes apply immediately (hear the new sound, switch device right away);
// the caller saves the settings when the player leaves.

void applyDisplaySettings(const Settings& settings); // fullscreen and frame rate: at startup, and when changed
void openSettingsScreen(const std::string& soundsDir); // refreshes the device and sound lists
bool settingsScreen(Settings& settings, const std::string& soundsDir); // true when the player pressed Back
