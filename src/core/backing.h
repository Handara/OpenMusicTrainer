#pragma once

#include "core/chart.h"

#include <string>
#include <vector>

// A backing for a chart with no audio of its own (a tab imported from Guitar Pro): every part played on lahn's
// synths (a plucked string for guitars, the synth bass for basses), and a soft click on every beat, the bar's first
// louder, standing in for the drums. Mono, from tick 0, with a moment after the last note. Pure logic.
std::vector<float> renderBacking(const Chart& chart, int sampleRate);

// A WAV file (16-bit), replaced whole: mono, or stereo with its samples left, right, left, right...
bool writeWav(const std::string& path, const std::vector<float>& samples, int sampleRate, std::string& error, int channels = 1);
// A WAV file read back (16, 24 or 32-bit, or 32-bit float), its channels mixed to one (as writeWav writes, the
// Instrument screen's checks, and the impulse responses players load)
bool readWav(const std::string& path, std::vector<float>& samples, int& sampleRate, std::string& error);
