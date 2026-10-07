#pragma once

#include "core/tonechain.h"

#include <string>

// Neural Amp Modeler captures (.nam files): a real amp or pedal as a neural network trained on how it sounds, played
// by the Capture effect (core/tonechain). NeuralAmpModelerCore (MIT) runs them. Players download them (TONE3000 has
// hundreds of thousands, free) and drop them on the tone wizard.

// A capture file ready to play: a model for each audio thread, loaded and settled off the audio thread. Kept for
// the session by path and by the effect's place in the tone (a model remembers what it played, so two Capture
// effects never share one); a file that can't be played too (nullptr, and why).
const ToneAsset* captureFromFile(const std::string& path, int slot, std::string& error);
// The tone's captures given their models, from the player's captures folder: before it's handed to the audio thread
void attachCaptures(ToneParameters& parameters, const std::string& folder);
