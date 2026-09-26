#pragma once

#include <string>

// The tuner screen: listens to the input device and shows the nearest note and how far off it is.

bool startTuner(const std::string& inputDevice, std::string& error); // opens the input device (empty = system default)
void updateTuner();                  // reads new input and detects the pitch; call once per frame
void drawTuner();                    // ImGui widgets, drawn inside the caller's window
void stopTuner();                    // closes the input device; safe to call more than once
