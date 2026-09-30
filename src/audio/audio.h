#pragma once

#include <atomic>
#include <string>
#include <vector>

// Audio output through miniaudio, in the same style as raylib's API: one audio system, plain functions.
// miniaudio stays hidden inside audio.cpp so its large header isn't compiled into every file.

// Devices are chosen by the name the system gives them; an empty or unknown name means the system default
bool initAudio(const std::string& outputDevice, std::string& error);
void closeAudio();
const char* audioBackendName(); // e.g. "PulseAudio", "WASAPI", "Null" (no real device)
std::vector<std::string> outputDeviceNames(); // asks the system: too slow to call every frame
std::vector<std::string> inputDeviceNames(); // Windows' own inputs, then the ASIO drivers, as "ASIO: <driver>"
bool setOutputDevice(const std::string& outputDevice, std::string& error); // restarts output: stops any song
const char* outputDeviceName(); // the device actually in use
double outputLatencySeconds();  // the output's buffering: how late sound leaves, at the least (the global offset covers it)
void setMasterVolume(float volume); // 0..1

// One song at a time, streamed from disk. The song's playback position is the game's master clock.
bool loadSong(const std::string& path, std::string& error);
void unloadSong();
void playSong(bool loop);
// Plays from a point in the song (seconds; before 0 it waits that long first), starting a moment from now on the
// engine's clock (audioTime). Returns the engine time at which `seconds` plays, so clicks and notes can be scheduled
// exactly with it; -1 if it can't (no song, or one from a reader, which only plays from the start).
double playSongFrom(double seconds);
void stopSong(); // pauses where it is; playSong or playSongFrom starts it again
bool songEnded();      // true once a non-looping song has played to its end
double songLength();   // seconds
double songPosition(); // seconds, smoothed between audio updates; call once per frame

// A song file's loudness over time, for drawing its waveform: the peak of every 1/peaksPerSecond of a second, 0 to 1.
// It decodes the whole file, which takes a second or two for a long song, so it's meant for a background thread:
// setting `cancel` stops it early (and it returns false). Independent of the playing song.
bool songPeaks(const std::string& path, int peaksPerSecond, std::vector<float>& out, const std::atomic<bool>& cancel);

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
void playPreviewAt(float frequency, double time); // at a time on the engine's clock (audioTime), like playClickAt
// A note played on the game's piano (the built-in electric piano), whatever the preview sound: keys parts sound
// it for every key the player presses, since most MIDI controllers and every computer keyboard make no sound
void playKeysNote(float frequency);
// A rhythm mode drum hit, now: the deep don or the rim's ka (core/synth renderDrum)
void playDrum(bool high);

// The audio engine's own clock in seconds, smoothed between its updates like songPosition: for anything that
// keeps time without a song (metronome, drills, calibration). Call it every frame.
double audioTime();
// A metronome click at a time on that clock (now, if the time has passed); accent = the first beat of a bar
void playClickAt(double time, bool accent);
void stopPreviews();
// The hit sound, osu!-style: a short drop on each note hit while playing an instrument, brighter for a perfect. Its
// own volume (0..1, 0 for none), since it plays over the player's own instrument.
void setHitSoundVolume(float volume);
void playHitSound(bool perfect);

// Input from a microphone, an instrument or an audio interface, every input the device has kept apart. Only runs
// between startCapture and stopCapture.
bool startCapture(const std::string& inputDevice, std::string& error);
void stopCapture();
// Hearing the instrument through lahn, wherever the player is in the game: its inputs, straight from the input device,
// through a small amp (core/tone: volume, drive, tone), to the speakers. `inputs`: which of the device's inputs (the
// guitar's and bass's); empty for every one but `excluded` (the voice's: a microphone in the speakers would howl).
// While it's on, the input device stays open between screens, and the screens that listen share it. It adds the
// input's and the output's buffering: an interface's own direct monitoring has none, but no amp.
bool setMonitor(bool on, const std::string& inputDevice, const std::vector<int>& inputs, int excluded, std::string& error);
void setMonitorTone(float volume, float drive, float tone); // each 0..1
bool monitorActive();
// Heard as a synth instead (the default): the speakers don't play the input; the main thread reads it (readMonitor,
// input/synthmonitor), finds the notes played, and plays them on the synth bass. Clean, a little later than the input.
void setMonitorSynth(bool synth);
int readMonitor(float* out, int maxFrames); // the input as the monitor mixed it (mono), since the last call
int monitorSampleRate();                    // its rate, 0 while it isn't listening
// The synth bass, one note at a time like the instrument: a new note fades the last one out as it starts
void playSynthNote(float frequency, float volume);
void releaseSynthNote(); // the string muted: the note fades out

// Windows: take the input device for lahn alone (WASAPI exclusive mode), past the effects Windows puts on
// microphones. Its noise suppression lets an instrument through only while someone speaks: on a Scarlett Solo, a
// bass alone came through near silent, and at full strength the moment someone sang. Other programs can't use the
// device while lahn listens. When it can't be had alone (another program has it that way), it's shared as usual.
// Takes effect at the next startCapture. Elsewhere it changes nothing.
void setExclusiveCapture(bool on);
bool captureIsExclusive(); // the device listened to now is lahn's alone
bool captureIsAsio();       // listening through an ASIO driver (an input device named "ASIO: ...")
double captureLatencySeconds(); // the input's buffering: how late samples reach lahn, at the least
void openInputDriverSettings(); // an ASIO driver's own settings window (its buffer size), while it's listening
int captureSampleRate();
const char* captureDeviceName();
// The device's inputs (an audio interface has several: a guitar on one, a microphone on another)
int captureChannels();
// Moves captured samples out, oldest first; returns how many frames. `channel` picks one input (from 0), -1 mixes
// them all into one.
int readCapture(float* out, int maxFrames, int channel = -1);
// Every input at once, interleaved (frame by frame, captureChannels() samples each): for looking at them side by side
int readCaptureAll(float* out, int maxFrames);
