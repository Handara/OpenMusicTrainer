#pragma once

#include <string>
#include <vector>

// ASIO: an audio interface's own low-latency driver on Windows (a Focusrite's, or ASIO4ALL for any device). It goes
// straight to the hardware: no Windows audio effects (whose noise suppression lets an instrument through only while
// someone speaks), no sharing, and buffers of a millisecond or two. Input only: lahn's sound still plays through
// Windows. The driver hands over each buffer on a thread of its own; it's converted at once and passed to `sink` as
// interleaved float frames, every input kept apart. One driver at a time. Elsewhere than Windows there are none.

using AsioSink = void (*)(const float* frames, int frameCount);

std::vector<std::string> asioDriverNames(); // the drivers installed (from the registry: nothing is loaded)

// Opens the driver and starts it, at `sampleRate` if it can (the output's rate, so one device isn't asked for two),
// else at its own. `sink` is called on the driver's thread: it must not block or allocate.
bool startAsioInput(const std::string& driver, double sampleRate, AsioSink sink, std::string& error);
void stopAsioInput(); // safe to call more than once
bool asioInputActive();

int asioInputChannels();
double asioInputSampleRate();
int asioBufferFrames();       // frames in each buffer the driver hands over
int asioInputLatencyFrames(); // the delay the driver reports: its buffer and its converters

// The driver asked to be started again: its settings changed (buffer size, sample rate) in its control panel
bool asioRestartRequested();
void openAsioControlPanel();  // the driver's own settings window, while it's open
