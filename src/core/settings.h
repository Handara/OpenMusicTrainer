#pragma once

#include "core/inputs.h"
#include "core/pianokeys.h"

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
    bool highwayFalls = false; // the highway's notes fall down columns (strings side by side) instead of scrolling across
    bool neck = false;    // osu!-style: rings closing onto the notes' places on a drawn fretboard (views/neckview)
    bool any() const { return staff || tab || highway || neck; }
};

struct Settings {
    // Audio
    std::string outputDevice;          // device name as the system reports it; empty = system default
    std::string inputDevice;
    // Which of the input device's inputs each instrument is plugged into (from 0); -1 = all of them mixed, for a
    // device with one input
    int guitarChannel = -1;
    int bassChannel = -1;
    int voiceChannel = -1;
    bool exclusiveInput = true;        // Windows: the input device for lahn alone, past Windows' effects (audio.h)
    std::string midiDevice;            // a MIDI keyboard or controller, by name; empty = the first one connected
    float masterVolume = 1.0f;         // 0..1
    float previewVolume = 0.6f;        // sounds the game makes itself (editor note previews, later ear training)
    std::string previewSound = "drop";  // a built-in sound, or a file name in the user's sounds folder
    float hitSoundVolume = 0.5f;       // the drop on each note hit with an instrument (0 for none)

    // Display
    NoteViews noteViews;
    bool lowStringOnTop = true;        // string order on the highway and in the editor: low E at the top (on the left when the
                                       // highway falls), or at the bottom like tab
    bool darkTheme = false;            // lahn's colors: light (the default) or dark
    bool fullscreen = false;
    int frameRateLimit = 60;           // 0 = unlimited

    // Gameplay
    bool playWithInstrument = false;   // judge notes from the input device instead of the number keys
    float noteSpeed = 300.0f;          // how fast notes scroll, pixels per second
    int globalOffsetMs = 0;            // output latency compensation: positive = notes are judged and drawn later
    int inputOffsetMs = 0;             // the input device's own delay, on top: positive = played notes arrive late
    std::vector<std::string> pianoKeys = defaultPianoKeys(); // the computer keys that play piano, from a C (core/pianokeys)
};

// Unlike charts, settings load leniently: they're the player's own file, and a typo or a line from a newer
// version must not throw away everything else. Problems are listed in `warnings`, and the rest still loads.
// A missing file isn't a problem at all: it just means default settings.
// The channel an instrument listens to
int channelFor(const Settings& settings, InputRole role);

Settings loadSettings(const std::string& path, std::vector<std::string>& warnings);
bool saveSettings(const std::string& path, const Settings& settings, std::string& error);
