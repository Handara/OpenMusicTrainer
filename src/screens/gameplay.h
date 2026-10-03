#pragma once

#include "core/chart.h"
#include "core/ranking.h"
#include "core/settings.h"
#include "raylib.h"

#include <string>

struct GameResult {
    std::string title;
    std::string partName;
    int score;
    int maxCombo;
    int perfectCount;
    int nearCount;
    int missCount;
    int totalNotes;
    float accuracy;          // osu!'s way (core/ranking)
    TimingStats timing;      // the average error and the unstable rate
    bool withInstrument;
    std::string fingerprint; // of the part played: its records are kept under it
    std::vector<float> errorsMs;  // every hit's timing (+ early), for its distribution
    Rectangle distributionFrom;   // where the distribution was on the play screen: the results grow it from there
    // Filled in once the run is recorded: where it placed among the part's runs (0 = a new best, -1 = not kept),
    // and the part's best runs to show beside it
    int place = -1;
    std::vector<RunRecord> records;
    std::vector<RunRecord> history; // every run of the part, as played: this one last
};

// Practice: a section of the song (whole bars) played over and over. Slower if asked (the song keeps its pitch:
// audio setSongSpeed), and each pass that plays every note moves the tempo up until it's the song's own, if asked;
// or note by note, the song waiting on each note until it's played. Nothing is recorded.
struct PracticeOptions {
    bool on = false;
    int fromTick = 0, toTick = 0; // the section
    float speed = 1.0f;           // its tempo, against the song's: 0.7 is 70%
    bool gradual = false;         // every note played at a tempo under the song's: the next pass is faster, by `step`
    float step = 0.05f;
    bool noteByNote = false;
    int passes = 0;               // how many; 0 for until a pass plays every note at the song's own tempo
};

// How the practice is going
struct PracticeProgress {
    int passes = 0;
    float speed = 1.0f;           // the tempo now
    float lastAccuracy = 0.0f;    // the share of the section's notes played in the last pass, 0 to 1
    float bestAccuracy = 0.0f;
    bool mastered = false;        // every note played, at the tempo it was aiming for
    double raisedAt = -100.0;     // when the last pass raised the tempo (GetTime)
};

struct GameplayOptions {
    int part = 0;              // which of the chart's parts to play
    bool rhythmMode = false;   // taiko-style: only the rhythm counts (core/rhythmmode)
    float noteSpeed = 300.0f;  // pixels per second
    float offsetSeconds = 0.0f; // latency compensation: positive = notes are judged and drawn later
    bool lowStringOnTop = true;
    bool video = true;         // the song's video behind the notes, when it has one
    NoteViews noteViews;
    bool playWithInstrument = false;  // judge notes played on the input device (the number keys work either way)
    InputRole instrument = InputRole::Guitar; // which: its input and its range are listened to. A part of another
                                              // instrument's (a guitar melody on a bass) counts its notes in any octave
    bool hitSounds = true;            // a key that hits plays its note (with an instrument, it's heard already)
    bool hitSoundIsNote = true;       // a note hit sounds as itself, on the part's own instrument (lahn's bass or guitar)...
    float hitSoundVolume = 0.5f;      // ...that loud (with an instrument; keys hitting play it at keyVolume)
    float keyVolume = 0.6f;
    std::string inputDevice;
    int guitarChannel = -1;           // the device's input each instrument is plugged into (-1: all mixed)
    int bassChannel = -1;
    std::string midiDevice;           // for keys parts: a MIDI keyboard (empty = the first connected)...
    std::vector<std::string> pianoKeys; // ...or else the computer keys that play piano (core/pianokeys)
    float inputOffsetSeconds = 0.0f;  // the input device's own delay (see calibration)
    PracticeOptions practice;
};

// The play screen: one song played once, judged against the chart's first fretted track.
// Audio must already be initialized (initAudio).

bool startGameplay(const std::string& chartPath, const GameplayOptions& options, std::string& error); // loads chart + audio, starts the song
// A chart already in memory (the editor's, maybe unsaved), from a tick on: the notes before it are left out, and the
// song starts a short lead-in before it
bool startGameplayWithChart(const Chart& chart, const std::string& audioPath, const GameplayOptions& options, int fromTick,
                            std::string& error);
bool updateGameplay(); // one frame of input and judging; returns false once the song is over
void drawGameplay();    // the note views, with raylib
void drawGameplayHud(); // the song, the score and the meters over them, with ImGui: between beginUiFrame and endUiFrame
// Pausing: the song stops where it is. Resuming plays it again from a little before, to get back into the rhythm
// (notes already judged stay judged). The game also pauses itself when its window loses focus.
void pauseGameplay();
void resumeGameplay();
bool gameplayPaused();
// Paused because the instrument sounds out of tune: its notes kept coming off the same way (core/tuningcheck).
// `cents`: by how much (+ sharp). Resuming plays on, out of tune, and isn't stopped for it again.
bool gameplayOutOfTune(float& cents);
void stopGameplay();   // stops and releases the song; safe to call more than once
GameResult gameplayResult();
bool gameplayPractising();
PracticeProgress practiceProgress(); // after a practice is over too: how it went
float gameplaySongTime();            // where in the song it is (seconds): to practise from there
