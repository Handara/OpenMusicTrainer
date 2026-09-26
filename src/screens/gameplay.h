#pragma once

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
    float noteSpeed = 300.0f;  // pixels per second
    float offsetSeconds = 0.0f; // latency compensation: positive = notes are judged and drawn later
    bool lowStringOnTop = true;
    NoteView noteView = NoteView::Highway;
};

// The play screen: one song played once, judged against the chart's first fretted track.
// Audio must already be initialized (initAudio).

bool startGameplay(const std::string& chartPath, const GameplayOptions& options, std::string& error); // loads chart + audio, starts the song
bool updateGameplay(); // one frame of input and judging; returns false once the song is over
void drawGameplay();
void stopGameplay();   // stops and releases the song; safe to call more than once
GameResult gameplayResult();
