#pragma once

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

// The play screen: one song played once, judged against the chart's first fretted track.
// Audio must already be initialized (initAudio).

bool startGameplay(const std::string& chartPath, std::string& error); // loads chart + audio, starts the song
bool updateGameplay(); // one frame of input and judging; returns false once the song is over
void drawGameplay();
void stopGameplay();   // stops and releases the song; safe to call more than once
GameResult gameplayResult();
