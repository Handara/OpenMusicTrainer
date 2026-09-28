#pragma once

#include "core/chart.h"
#include "core/settings.h"

#include <string>

struct GameResult {
    std::string title;
    int score;
    int maxCombo;
    int perfectCount;
    int nearCount;
    int missCount;
    int totalNotes;
};

struct GameplayOptions {
    int part = 0;              // which of the chart's fretted tracks to play
    float noteSpeed = 300.0f;  // pixels per second
    float offsetSeconds = 0.0f; // latency compensation: positive = notes are judged and drawn later
    bool lowStringOnTop = true;
    NoteViews noteViews;
    bool playWithInstrument = false;  // judge notes played on the input device (the number keys work either way)
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
void stopGameplay();   // stops and releases the song; safe to call more than once
GameResult gameplayResult();
