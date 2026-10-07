#pragma once

#include "core/tonechain.h"

#include <string>
#include <vector>

// Speaker cabinets, as amp simulators hear them: an impulse response (the sound of one click through the speaker and
// the mic in front of it), which the tone chain plays everything through (convolution). It's most of what makes an
// amp sound like a recorded amp: a speaker can't follow the highs a distorted signal is full of, and its cone and
// box give the mids their shape. The built-in ones are lahn's own, each designed as a speaker's response (where its
// lows start, its cone's peaks and dips, where its top falls away) and made into the impulse response that sounds
// that way with nothing played ahead of time (minimum phase). Pure: the tone screen prepares them, off the audio
// thread.

int cabinetCount();
const char* const* cabinetNames(); // "1x12 Open"...: what the cabinet's Speaker knob shows

// A built-in cabinet's impulse response at a sample rate: about 43 ms of it, its loudest frequencies at 0 dB
std::vector<float> cabinetResponse(int cabinet, int sampleRate);
// Its response in dB at a frequency, as designed (for the tests, and for drawing it)
float cabinetDb(int cabinet, float frequency);

// What the audio thread plays a cabinet through: its response at the rates a device may run at. Built once a
// session, kept to the end (the audio thread may be playing it).
const ToneAsset& builtInCabinet(int cabinet);
// A player's impulse response (a WAV file, as cabinet makers and TONE3000 share them), ready to play like a built-in
// one: any silence before it starts cut (it would only be delay), as long as theirs (eased out), at each rate, its
// loudest frequencies at 0 dB. Kept for the session by path, a file that can't be read too (nullptr, and why).
const ToneAsset* cabinetFromFile(const std::string& path, std::string& error);
// The tone's cabinets given what they play through (the rest untouched): before it's handed to the audio thread. A
// cabinet with a file plays it from `folder` (the built-in speaker chosen if it can't be read).
void attachCabinets(ToneParameters& parameters, const std::string& folder = "");

// Any impulse response to another sample rate (windowed sinc), for a file recorded at a rate the device isn't at
std::vector<float> resampleResponse(const std::vector<float>& taps, int fromRate, int toRate);
