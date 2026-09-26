#pragma once

#include <string>

// Audio output through miniaudio, in the same style as raylib's API: one audio system, plain functions.
// miniaudio stays hidden inside audio.cpp so its large header isn't compiled into every file.

bool initAudio(std::string& error);
void closeAudio();
const char* audioBackendName(); // e.g. "PulseAudio", "WASAPI", "Null" (no real device)

// One song at a time, streamed from disk. The song's playback position is the game's master clock.
bool loadSong(const std::string& path, std::string& error);
void unloadSong();
void playSong(bool loop);
double songLength();   // seconds
double songPosition(); // seconds, smoothed between audio updates; call once per frame
