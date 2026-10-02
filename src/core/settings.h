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
// What a note on the neck says: its fret, its note's name (to learn the neck by), or both
enum class NoteLabel { Fret, Name, Both };

struct NoteViews {
    bool staff = false;   // sheet music
    bool neck = true;     // osu!-style: rings closing onto the notes' places on a drawn fretboard (views/neckview)
    NoteLabel label = NoteLabel::Both;
    bool wholeNeck = true; // the neck view shows the whole neck, from the nut up; else just the frets the song uses
    bool any() const { return staff || neck; }
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
    float previewVolume = 0.6f;        // sounds the game makes itself (the keyboard's notes, ear training)
    // The song editor's own mix, set on its screen: the notes it plays (each part on its own instrument, a bass or a
    // clean guitar) and the song under them
    float editorNoteVolume = 0.8f;
    float editorSongVolume = 1.0f;
    bool editorNoteNames = true;       // each note's name in it, under its fret, in the editor
    std::string previewSound = "drop";  // a built-in sound, or a file name in the user's sounds folder
    float hitSoundVolume = 0.5f;       // the drop on each note hit with an instrument (0 for none)
    // Hearing the instrument through lahn wherever the player is (audio.h: setMonitor)
    bool monitorOn = true;
    bool monitorSynth = false;         // heard as a synth bass playing the notes found (input/synthmonitor), not its own sound
    float monitorVolume = 0.8f;
    // The tone its own sound goes through (core/tonelibrary), one per instrument: the player's or built in. The one heard
    // is the instrument played last (a song's part, the Instrument screen's tab, the tone wizard's).
    std::string bassTone = "Clean";
    std::string guitarTone = "Clean";
    InputRole heardInstrument = InputRole::Bass;
    std::string& toneFor(InputRole role){ return role == InputRole::Guitar ? guitarTone : bassTone; }
    const std::string& toneFor(InputRole role) const { return role == InputRole::Guitar ? guitarTone : bassTone; }

    // Display
    NoteViews noteViews;
    bool lowStringOnTop = true;        // string order on the highway and in the editor: low E at the top (on the left when the
                                       // highway falls), or at the bottom like tab
    bool darkTheme = false;            // lahn's colors: light (the default) or dark
    bool songVideo = true;             // a song's video, when it has one, behind the notes
    bool fullscreen = false;
    int frameRateLimit = 60;           // 0 = unlimited

    // Gameplay
    bool playWithInstrument = false;   // judge notes from the input device instead of the number keys
    InputRole playInstrument = InputRole::Guitar; // which instrument, playing with one: its input, its range
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
