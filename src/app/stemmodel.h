#pragma once

#include <string>
#include <vector>

// The stems add-on's model, run: KUIELab's MDX-Net bass model, through ONNX Runtime. lahn isn't built with either: the
// add-on (core/addon) brings the runtime as a library, loaded here when it's there, and the model beside it. So a
// lahn without the add-on is no bigger, and still whole.

const char* const STEMS_ADDON = "stems";

bool stemsAddonInstalled(const std::string& addonsDir);
// Loads the runtime and the model (a second or two); false, with why, if the add-on is missing or broken
bool openStemModel(const std::string& addonsDir, std::string& error);
// A chunk's spectrogram in, the bass's out (core/stemsplit's StemModel). Seconds of work, on the caller's thread.
bool runStemModel(const std::vector<float>& input, std::vector<float>& output);
std::string stemModelError(); // why the last run failed
void closeStemModel();        // safe to call more than once
