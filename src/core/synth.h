#pragma once

// Sound generation. Pure math: fills sample buffers, playing them is the audio layer's job.

// A plucked string (Karplus-Strong): a burst of noise circulating in a delay line one period long.
// Each pass through the loop smooths it a little, so it decays and mellows like a real string.
void renderPluck(float* out, int count, float frequency, int sampleRate, unsigned seed);

// A sine tone with a soft attack and a few gentle harmonics, like a soft bell
void renderSoftTone(float* out, int count, float frequency, int sampleRate);

// FM synthesis (the DX7 technique): one sine wave wobbles another's phase. The wobble fades faster than
// the sound, so each note starts bright and mellows, like an electric piano.
void renderKeys(float* out, int count, float frequency, int sampleRate);

// A water drop's "plip": a tone that glides quickly up into its pitch and dies away fast
void renderDrop(float* out, int count, float frequency, int sampleRate);

// A fingered electric bass: a round fundamental, a brief brighter attack as the finger leaves the string (the upper
// harmonics dying away first), a soft thump, and a long decay. For hearing a bass played through the game as a clean
// synth (the notes it heard, see input/synthmonitor).
void renderBass(float* out, int count, float frequency, int sampleRate);

// A note on a string instrument, for hearing a part as its own instrument (the song editor's notes): an electric bass
// played with the fingers, round at the bottom with a little slap in its attack, or a clean electric guitar, bright and
// bell-like with a light chorus. Built from the string itself: each harmonic as loud as plucking there and listening
// at the pickup makes it, the high ones dying first. No distortion. Every note comes out equally loud whatever its
// pitch; it rings for `count` samples and is muted over the last few hundredths of a second, as a hand does.
enum class StringVoice { Guitar, Bass };
void renderStringNote(float* out, int count, float frequency, int sampleRate, StringVoice voice, unsigned seed);

// A metronome click: a high woodblock-like tone that dies within ~30 ms, plus a tiny burst of noise for the
// "tick". The accented click (the first beat of a bar) is higher.
void renderClick(float* out, int count, int sampleRate, bool accent);

// A taiko-style drum hit for rhythm mode. Low (don): a tone sweeping down from 170 Hz to 60 Hz like a drum skin,
// over a short thump of noise. High (ka): the rim, a bright tick of noise and tone that's gone in about 40 ms.
void renderDrum(float* out, int count, int sampleRate, bool high);

// A drum kit's drums, for the games' tunes: a kick (a deep tone sweeping down, a click on top), a snare (two tones
// and a burst of bright noise), a hi-hat closed and open (metal: square waves at clashing pitches and high noise) and
// a crash (the same, long)
enum class KitDrum { Kick, Snare, Hat, OpenHat, Crash };
void renderKitDrum(float* out, int count, int sampleRate, KitDrum drum, unsigned seed);

// A crowd, for the games' verdicts, Rhythm Heaven style: cheering (about twenty voices going "yay!", each its own
// pitch and voice, gliding up from the y, with clapping and a whistle), clapping politely (a few hands, no voices,
// quieter) or going "awww" (the voices falling, an "aw" vowel). Each voice a buzz through its vowel's formants
// (resonances at a vowel's frequencies), in a small room.
enum class CrowdReaction { Cheer, Claps, Aww };
void renderCrowd(float* out, int count, int sampleRate, CrowdReaction reaction, unsigned seed);

// Renders one of BUILT_IN_PREVIEW_SOUNDS by name (see core/settings.h); false if the name isn't one of them
bool renderBuiltInSound(const char* name, float* out, int count, float frequency, int sampleRate, unsigned seed);
