#pragma once

#include <string>
#include <vector>

// Song data as stored in a .chart file. Read-only once loaded: gameplay state lives elsewhere.
// All musical time is in ticks, `resolution` ticks per beat.

struct TempoChange {
    int tick;
    double bpm;
};

struct FrettedNote {
    int tick;
    int stringIndex; // 0 = lowest-pitched string
    int fret;        // 0 = open string
    int duration;    // sustain length in ticks, 0 = no sustain
};

enum class InstrumentType { Guitar, Bass };

// Guitar and bass share this shape; they differ only by tuning and how they're presented
struct FrettedTrack {
    InstrumentType type;
    std::string name;
    std::vector<int> tuning;        // MIDI pitch per string, lowest first; its size is the string count
    std::vector<FrettedNote> notes; // sorted by tick
};

struct Chart {
    int version;
    std::string title;
    std::string artist;
    int resolution; // ticks per beat
    double offset;  // seconds into the audio where tick 0 falls
    int endTick;
    std::vector<TempoChange> tempoMap; // sorted by tick, first entry at tick 0
    std::vector<FrettedTrack> frettedTracks;
};

double tickToSeconds(const Chart& chart, int tick);
