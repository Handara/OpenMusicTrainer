#pragma once

#include <string>
#include <vector>

// The player's preferences, stored as "key value" lines in settings.txt in the user data folder.

const char* const BUILT_IN_PREVIEW_SOUNDS[] = { "pluck", "soft", "keys", "drop" };
const int BUILT_IN_PREVIEW_SOUND_COUNT = 4;

// How notes are shown while playing: any mix of the views, stacked top to bottom (sheet music, tab, then the
// highway). At least one is always on.
struct NoteViews {
    bool staff = false;   // sheet music
    bool tab = false;
    bool highway = true;
    bool any() const { return staff || tab || highway; }
};

struct Settings {
    // Audio
    std::string outputDevice;          // device name as the system reports it; empty = system default
    std::string inputDevice;
    float masterVolume = 1.0f;         // 0..1
    float previewVolume = 0.6f;        // sounds the game makes itself (editor note previews, later ear training)
    std::string previewSound = "pluck"; // a built-in sound, or a file name in the user's sounds folder

    // Display
    NoteViews noteViews;
    bool lowStringOnTop = true;        // string order on the highway and in the editor: low E at the top, or at the bottom like tab
    bool fullscreen = false;
    int frameRateLimit = 60;           // 0 = unlimited

    // Gameplay
    bool playWithInstrument = false;   // judge notes from the input device instead of the number keys
    float noteSpeed = 300.0f;          // how fast notes scroll, pixels per second
    int globalOffsetMs = 0;            // output latency compensation: positive = notes are judged and drawn later
    int inputOffsetMs = 0;             // the input device's own delay, on top: positive = played notes arrive late
};

// Unlike charts, settings load leniently: they're the player's own file, and a typo or a line from a newer
// version must not throw away everything else. Problems are listed in `warnings`, and the rest still loads.
// A missing file isn't a problem at all: it just means default settings.
Settings loadSettings(const std::string& path, std::vector<std::string>& warnings);
bool saveSettings(const std::string& path, const Settings& settings, std::string& error);
