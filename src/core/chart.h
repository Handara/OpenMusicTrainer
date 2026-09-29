#pragma once

#include "core/notation.h"

#include <string>
#include <vector>

const int MAX_FRET = 24;

// Song data as stored in a .chart file. Read-only once loaded: gameplay state lives elsewhere.
// All musical time is in ticks, `resolution` ticks per quarter note; tempos count quarter notes per minute,
// whatever the time signature (as in MIDI).

struct TempoChange {
    int tick;
    double bpm;
};

// Only for writing the music down (bar lines, note values, beams): timing comes from ticks and tempos alone
struct TimeSignatureChange {
    int tick;
    int beats;    // 3 in 3/4
    int beatUnit; // 4 in 3/4: the note value of a beat, as a fraction of a whole note (1, 2, 4, 8, 16 or 32)
};

struct KeyChange {
    int tick;
    KeySignature key;
};

struct FrettedNote {
    int tick;
    int stringIndex; // 0 = lowest-pitched string
    int fret;        // 0 = open string
    int duration;    // sustain length in ticks, 0 = no sustain
};

enum class InstrumentType { Guitar, Bass, Keys }; // a fretted track is Guitar or Bass; Keys is a KeysTrack's

// Guitar and bass share this shape; they differ only by tuning and how they're presented
struct FrettedTrack {
    InstrumentType type = InstrumentType::Guitar;
    std::string name;
    std::vector<int> tuning;        // MIDI pitch per string, lowest first; its size is the string count
    std::vector<FrettedNote> notes; // sorted by tick
};

// A piano or keyboard part, played on a MIDI keyboard: its notes are pitches, not strings and frets
struct KeysNote {
    int tick;
    int pitch;       // MIDI note: 60 = middle C
    int duration;    // how long it's held, in ticks; 0 = not held
};

struct KeysTrack {
    std::string name;
    std::vector<KeysNote> notes; // sorted by tick, then pitch
};

struct Chart {
    int version;
    std::string title;
    std::string artist;
    std::string audioFile; // relative to the chart file's folder, empty if the chart has none
    int resolution; // ticks per beat
    double offset;  // seconds into the audio where tick 0 falls
    int endTick;
    std::vector<TempoChange> tempoMap; // sorted by tick, first entry at tick 0
    std::vector<TimeSignatureChange> timeSignatures; // sorted, first at tick 0, each on a bar line; 4/4 if the file has none
    std::vector<KeyChange> keys;                     // sorted, first at tick 0, each on a bar line; C major if none
    std::vector<FrettedTrack> frettedTracks;
    std::vector<KeysTrack> keysTracks;
};

// A song's parts are its fretted tracks, then its keys tracks: part numbers count through both in that order
int partCount(const Chart& chart);
bool isKeysPart(const Chart& chart, int part);
std::string partName(const Chart& chart, int part);

// Reads and validates a .chart file. On failure returns false and sets `error` to "path:line: message".
bool loadChart(const std::string& path, Chart& out, std::string& error);

// Writes a chart in the same format loadChart reads. The file is replaced only once the new one is fully
// written, so a crash or full disk mid-save never leaves a half-written chart behind.
bool saveChart(const std::string& path, const Chart& chart, std::string& error);

double tickToSeconds(const Chart& chart, int tick);
// The other way: a time in the audio to a tick, with a fraction. Before tick 0 it goes negative at the first tempo.
double secondsToTick(const Chart& chart, double seconds);

int ticksPerBar(const Chart& chart, const TimeSignatureChange& time);
const TimeSignatureChange& timeSignatureAt(const Chart& chart, int tick);
// Bars counted from 0. Past the chart's end, the last time signature carries on.
int barStartTick(const Chart& chart, int bar);
int barNumberAt(const Chart& chart, int tick);
// Where each bar starts, from tick 0 to the chart's end (included when it falls on a bar line)
std::vector<int> barTicks(const Chart& chart);
