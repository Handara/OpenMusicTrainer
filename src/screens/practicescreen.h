#pragma once

#include "core/songlibrary.h"
#include "screens/gameplay.h"

#include <string>

// Practising a part of a song: its bars on a timeline, the part's notes on them, and a stretch of it chosen on
// it, beat by beat (dragged across, or its edges dragged; the arrows move it), which can be listened to; then how: the tempo to start at, faster after
// each pass that plays every note (until the song's own), note by note, and how many passes. The choices are kept
// for the next time, the section for this song.
// `aroundSeconds`: the bars to choose first, around that place in the song (from the pause menu), -1 to keep the
// last ones. `message`: how the last practice went.
bool openPracticeScreen(const SongEntry& song, int part, double aroundSeconds, const std::string& message, std::string& error);

enum class PracticeChoice { None, Start };
PracticeChoice practiceScreen();
PracticeOptions practiceChoice(); // what was chosen, for GameplayOptions::practice
void closePracticeScreen();       // stops listening and lets the song go: leaving the screen any way but Start
// The metronome switched in the pause menu: the next practice starts with it as it was left
void rememberPracticeMetronome(bool on);
