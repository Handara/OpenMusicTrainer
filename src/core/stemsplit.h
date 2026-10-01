#pragma once

#include <atomic>
#include <functional>
#include <string>
#include <vector>

// Taking one instrument out of a song (its "stem"): the work around KUIELab's MDX-Net models (MIT code, CC-BY 4.0
// weights). The song is cut into overlapping chunks; each is turned into its spectrogram (a short-time Fourier
// transform), which the model, a neural network, redraws with only its instrument left; that's turned back into
// sound, and the chunks' middles put end to end. The model itself is run by whoever has it (app/stemmodel, from the
// stems add-on): here it's a function, so this is pure logic, testable with a stand-in.

const int STEM_RATE = 44100;          // the models were trained on 44.1 kHz stereo
const int STEM_BINS = 2048;           // of each frame's 8193 frequencies, the lowest the bass model sees (up to 5.5 kHz)
const int STEM_FRAMES = 512;          // frames in a chunk: about 11.9 s
const int STEM_TENSOR = 4 * STEM_BINS * STEM_FRAMES; // left real, left imaginary, right real, right imaginary

// The model: a chunk's spectrogram in, its instrument's out, both [4][STEM_BINS][STEM_FRAMES] row by row. False stops.
using StemModel = std::function<bool(const std::vector<float>& input, std::vector<float>& output)>;

// The stem of a stereo song at STEM_RATE. `progress` goes from 0 to 1 as it works (null for none); `cancel` stops it.
bool splitStem(const std::vector<float>& left, const std::vector<float>& right, const StemModel& model,
               std::vector<float>& stemLeft, std::vector<float>& stemRight, std::atomic<float>* progress,
               const std::atomic<bool>& cancel, std::string& error);
