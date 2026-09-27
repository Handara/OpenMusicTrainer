#pragma once

#include <string>
#include <vector>

// Audio output through miniaudio, in the same style as raylib's API: one audio system, plain functions.
// miniaudio stays hidden inside audio.cpp so its large header isn't compiled into every file.

// Devices are chosen by the name the system gives them; an empty or unknown name means the system default
bool initAudio(const std::string& outputDevice, std::string& error);
void closeAudio();
const char* audioBackendName(); // e.g. "PulseAudio", "WASAPI", "Null" (no real device)
std::vector<std::string> outputDeviceNames(); // asks the system: too slow to call every frame
std::vector<std::string> inputDeviceNames();
bool setOutputDevice(const std::string& outputDevice, std::string& error); // restarts output: stops any song
const char* outputDeviceName(); // the device actually in use
void setMasterVolume(float volume); // 0..1

// One song at a time, streamed from disk. The song's playback position is the game's master clock.
bool loadSong(const std::string& path, std::string& error);
void unloadSong();
void playSong(bool loop);
bool songEnded();      // true once a non-looping song has played to its end
double songLength();   // seconds
double songPosition(); // seconds, smoothed between audio updates; call once per frame

// A song whose samples come from a function instead of a file: the sound of a video, decoded as it plays. The
// function runs on the audio thread, so it must only work from memory, never wait on anything: it fills `out` with
// up to `frames` interleaved float frames and returns how many it wrote (fewer means the end). It's called until
// unloadSong(), which must come before whatever the function reads from is freed.
using SongReader = int (*)(void* user, float* out, int frames);
bool loadSongFromReader(SongReader reader, void* user, int sampleRate, int channels, double lengthSeconds, std::string& error);

// Short sounds at a pitch, for previews (e.g. the editor). Several can overlap.
// The sound is a built-in one (core/settings.h) or a file in the sounds folder; files with a clear pitch
// are re-pitched to each note, others play as they are.
std::vector<std::string> previewSoundNames(const std::string& soundsDir); // built-ins first, then files
bool setPreviewSound(const std::string& name, const std::string& soundsDir, std::string& error);
const char* previewSoundName();
bool previewSoundHasPitch();
void setPreviewVolume(float volume); // 0..1
void playPreview(float frequency, float delaySeconds = 0.0f); // the delay is timed on the audio clock, to the sample

// The audio engine's own clock in seconds, smoothed between its updates like songPosition: for anything that
// keeps time without a song (metronome, drills, calibration). Call it every frame.
double audioTime();
// A metronome click at a time on that clock (now, if the time has passed); accent = the first beat of a bar
void playClickAt(double time, bool accent);
void stopPreviews();

// Input from the default microphone / instrument, mono. Only runs between startCapture and stopCapture.
bool startCapture(const std::string& inputDevice, std::string& error);
void stopCapture();
int captureSampleRate();
const char* captureDeviceName();
int readCapture(float* out, int maxFrames); // moves captured samples out, oldest first; returns how many
