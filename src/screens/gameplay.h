#pragma once

#include "core/chart.h"
#include "core/ranking.h"
#include "core/settings.h"

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
    // Filled in once the run is recorded: where it placed among the part's runs (0 = a new best, -1 = not kept),
    // and the part's best runs to show beside it
    int place = -1;
    std::vector<RunRecord> records;
};

struct GameplayOptions {
    int part = 0;              // which of the chart's fretted tracks to play
    float noteSpeed = 300.0f;  // pixels per second
    float offsetSeconds = 0.0f; // latency compensation: positive = notes are judged and drawn later
    bool lowStringOnTop = true;
    NoteViews noteViews;
    bool playWithInstrument = false;  // judge notes played on the input device (the number keys work either way)
    bool hitSounds = true;            // a key that hits plays its note (with an instrument, it's heard already)
    std::string inputDevice;
    float inputOffsetSeconds = 0.0f;  // the input device's own delay (see calibration)
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
void stopGameplay();   // stops and releases the song; safe to call more than once
GameResult gameplayResult();
