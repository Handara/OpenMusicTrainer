#pragma once

#include <string>
#include <vector>

// ASIO: an audio interface's own low-latency driver on Windows (a Focusrite's, or ASIO4ALL for any device). It goes
// straight to the hardware: no Windows audio effects (whose noise suppression lets an instrument through only while
// someone speaks), no sharing, buffers of a millisecond or two. Both ways at once, as Ableton uses it: in each call
// the driver hands over what its inputs heard and takes what its outputs play, so an input can be heard back one
// buffer later. Its thread calls `sink` with the inputs (interleaved float frames, every input kept apart), then
// `render` for the outputs (interleaved stereo, to fill). One driver at a time. Elsewhere than Windows there are none.

using AsioSink = void (*)(const float* frames, int frameCount);
using AsioRender = void (*)(float* stereoFrames, int frameCount);

std::vector<std::string> asioDriverNames(); // the drivers installed (from the registry: nothing is loaded)

// Opens the driver and starts it, at `sampleRate` if it can (else its own). `render`: nullptr for its inputs only.
// A driver that can't do both is opened for its inputs alone (asioOutputChannels() is then 0). Both callbacks run on
// the driver's thread: they must not block or allocate.
bool startAsio(const std::string& driver, double sampleRate, AsioSink sink, AsioRender render, std::string& error);
void stopAsio(); // safe to call more than once
bool asioActive();

int asioInputChannels();
int asioOutputChannels();     // 0: lahn's sound isn't going out through the driver
double asioSampleRate();
int asioBufferFrames();       // frames in each buffer the driver hands over
int asioInputLatencyFrames(); // the delays the driver reports: its buffers and its converters
int asioOutputLatencyFrames();

// The driver asked to be started again: its settings changed (buffer size, sample rate) in its control panel
bool asioRestartRequested();
void openAsioControlPanel();  // the driver's own settings window, while it's open
