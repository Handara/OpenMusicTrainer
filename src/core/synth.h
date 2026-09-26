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

// A metronome click: a high woodblock-like tone that dies within ~30 ms, plus a tiny burst of noise for the
// "tick". The accented click (the first beat of a bar) is higher.
void renderClick(float* out, int count, int sampleRate, bool accent);

// Renders one of BUILT_IN_PREVIEW_SOUNDS by name (see core/settings.h); false if the name isn't one of them
bool renderBuiltInSound(const char* name, float* out, int count, float frequency, int sampleRate, unsigned seed);
