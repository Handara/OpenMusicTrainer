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
bool songEnded();      // true once a non-looping song has played to its end
double songLength();   // seconds
double songPosition(); // seconds, smoothed between audio updates; call once per frame

// A short plucked-string sound at a pitch, for previews (e.g. the editor). Several can overlap.
void playPluck(float frequency);

// Input from the default microphone / instrument, mono. Only runs between startCapture and stopCapture.
bool startCapture(std::string& error);
void stopCapture();
int captureSampleRate();
const char* captureDeviceName();
int readCapture(float* out, int maxFrames); // moves captured samples out, oldest first; returns how many
