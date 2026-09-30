#pragma once

#include "core/settings.h"

#include <string>

// Latency calibration: clicks play, the player taps (or plays a note) along, and the typical delay is measured.
//   Tap: Space on each click. Measures how late sound reaches the player: the global offset.
//   Instrument: a note on each click. Measures the output delay plus the input device's: minus the global
//   offset, that's the input offset. So tapping comes first.

enum class CalibrationMode { Tap, Instrument };

bool startCalibration(CalibrationMode mode, const Settings& settings, std::string& error);

struct CalibrationChoice {
    bool apply = false;
    int offsetMs = 0; // when applying: the new global offset (Tap) or input offset (Instrument)
};
CalibrationChoice calibrationScreen();
void stopCalibration(); // safe to call more than once
