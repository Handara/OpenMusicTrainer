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
    // What a note hit in the game sounds like: "" for the part's own instrument (lahn's bass or clean guitar), or one
    // of HIT_SOUNDS. In the file: "hit_sound <name>" after the tuning, left out for the instrument.
    std::string hitSound;
};
const char* const HIT_SOUNDS[] = { "bass", "guitar", "pluck", "soft", "keys", "drop", "none" };

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
    // The song trimmed: the part of the audio that's played, in seconds into it. Nothing is cut from the file; the
    // game starts and stops there, and leaves out the notes that fall outside. 0 = not trimmed at that end.
    double trimStart = 0.0;
    double trimEnd = 0.0;
    // A video to show behind the notes while the song plays, in the chart's folder (MPEG-1: see src/video); empty
    // for none. A song plays the same without it: a chart naming a video that isn't there just has no picture.
    std::string videoFile;
    double videoOffset = 0.0; // seconds into the video where the audio starts: 0 when they were made together
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

// Parts brought into a song from another chart (a tab, another song's): the fretted tracks numbered in `parts` are
// added after the song's own, their notes on the same bars and beats (ticks are rescaled when the two charts count
// a beat differently). `withBars`: the other chart's tempos, time signatures and keys replace the song's; and its
// offset too if it has audio it was lined up with (a tab has none: the song keeps where its own first bar starts).
// The song then has the other chart's length in bars (its own was counted at the old tempo), and either way is made
// long enough for every note in it. A part named like one the song has gets a number after its name ("Bass 2").
void importParts(Chart& into, const Chart& from, const std::vector<int>& parts, bool withBars);

// Without the notes the trim leaves out: those before the song's start or from its end on, in every part
void dropTrimmedNotes(Chart& chart);

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
