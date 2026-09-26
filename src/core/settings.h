#pragma once

#include <string>
#include <vector>

// The player's preferences, stored as "key value" lines in settings.txt in the user data folder.

const char* const BUILT_IN_PREVIEW_SOUNDS[] = { "pluck", "soft", "keys", "drop" };
const int BUILT_IN_PREVIEW_SOUND_COUNT = 4;

struct Settings {
    // Audio
    std::string outputDevice;          // device name as the system reports it; empty = system default
    std::string inputDevice;
    float masterVolume = 1.0f;         // 0..1
    float previewVolume = 0.6f;        // sounds the game makes itself (editor note previews, later ear training)
    std::string previewSound = "pluck"; // a built-in sound, or a file name in the user's sounds folder

    // Display
    bool fullscreen = false;
    int frameRateLimit = 60;           // 0 = unlimited

    // Gameplay
    float noteSpeed = 300.0f;          // how fast notes scroll, pixels per second
    int globalOffsetMs = 0;            // latency compensation: positive = notes are judged and drawn later
};

// Unlike charts, settings load leniently: they're the player's own file, and a typo or a line from a newer
// version must not throw away everything else. Problems are listed in `warnings`, and the rest still loads.
// A missing file isn't a problem at all: it just means default settings.
Settings loadSettings(const std::string& path, std::vector<std::string>& warnings);
bool saveSettings(const std::string& path, const Settings& settings, std::string& error);
