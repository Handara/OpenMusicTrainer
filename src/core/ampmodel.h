#pragma once

#include "core/filters.h"

// The tone chain's amp and drive, built the way amp simulators build theirs, from the circuits they stand for:
//
// - Amp: tube gain stages one after the other (each pushed, clipped softly and a little lopsided as a triode is,
//   then its coupling capacitor losing some lows and the tube's own capacitance some highs), the amp's real tone
//   stack (the passive bass, middle and treble network of a Fender, a Marshall..., worked out from its parts as
//   D.T. Yeh and J.O. Smith did), then the power amp, which sags (its supply droops on hard playing: the squash of a
//   tube amp pushed) before presence and depth.
// - Drive: an overdrive (the Tube Screamer's: only the mids clipped, the lows passing clean), a distortion (the
//   RAT's: an op-amp's gain into hard diodes) or a fuzz (the Big Muff's: two clipping stages, mids scooped).
//
// The clipping runs four times faster than the device (oversampled): a distorted sound is full of harmonics higher
// than the device's rate can carry, which would otherwise fold back down as the harsh, digital fizz that cheap amp
// sims have. Every filter here only looks back, so nothing waits: no delay is added, beyond a sample or so of the
// oversampling's filters. Pure; no allocation (the audio thread runs them).

const int OVERSAMPLING = 4;

// Up to four times the rate and back: steep low-passes (eighth-order Butterworth) before and after the clipping
struct Oversampler {
    Biquad up[4], down[4];
};
void setOversampler(Oversampler& oversampler, float rate);

// A one-pole filter: a coupling capacitor's high-pass, or a tube's low-pass
struct OnePole {
    float coefficient = 0.0f, state = 0.0f, last = 0.0f;
};

const int MAX_AMP_STAGES = 4;

struct AmpState {
    int model = -1;
    float stageGain = 1.0f, bias = 0.0f, inputTrim = 1.0f, powerDrive = 1.0f, sag = 0.0f, level = 1.0f, cleanBlend = 0.0f;
    int stages = 2;
    Biquad tight, bright;              // before the stages, at the device's rate
    Oversampler oversampler;
    OnePole coupling[MAX_AMP_STAGES], miller[MAX_AMP_STAGES];
    double stackB[4] = {}, stackA[4] = {}, stackZ[3] = {}; // the tone stack: third order, its memory
    float stackMakeup = 1.0f;
    float sagEnvelope = 0.0f, sagAttack = 0.0f, sagRelease = 0.0f;
    Biquad presence, depth, cleanLow;  // after, at the device's rate
};

int ampModelCount();
const char* const* ampModelNames(); // what the Amp's Model knob shows: "Clean US", "Crunch UK"...
// The amp's knobs (core/tonechain's: model, gain, bass, mid, treble, presence; the tone knobs from 0 to 1, as the
// amp's own pots), worked out for a rate. Its memory is kept, so turning a knob doesn't click.
void setUpAmp(AmpState& amp, const float* values, float rate);
float processAmp(AmpState& amp, float x);
// The tone stack alone, in dB at a frequency (knobs 0 to 1): for the tests, and to see what the knobs do
float toneStackDb(int model, float treble, float mid, float bass, float frequency, float rate);

enum class DriveType { Overdrive, Distortion, Fuzz, Count };
const char* const* driveTypeNames(); // what the Drive's Type knob shows

struct DriveState {
    int type = -1;
    float gain = 1.0f, level = 1.0f, blend = 1.0f;
    Oversampler oversampler;
    Biquad preHigh, preLow, toneLow, toneHigh, cutLow; // the type's own filters around the clipping
    OnePole stageHigh[2];
    float toneMix = 0.5f;
};
// The drive's knobs (core/tonechain's: drive, type, tone, level, blend)
void setUpDrive(DriveState& drive, const float* values, float rate);
float processDrive(DriveState& drive, float x);
