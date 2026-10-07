#pragma once

#include "core/ampmodel.h"
#include "core/filters.h"

#include <memory>
#include <string>
#include <vector>

// A tone: the instrument's own sound shaped by a chain of effects, in order, like pedals into an amp or the effects
// on a DAW's track. Every effect works sample by sample and never looks ahead, so the chain adds no delay: the
// instrument is heard the moment it's played, whatever the tone. Tones are kept as small text files, to save, share
// and load. Pure; the audio thread runs a chain without allocating.

enum class EffectType { Gate, Compressor, Drive, Amp, Equalizer, Octaver, Chorus, Delay, Reverb, Cabinet, Capture, Count };

const int MAX_PARAMETERS = 6;
const int MAX_EFFECTS = 12;
const int MAX_CABINET_TAPS = 4096; // a cabinet's impulse response, at most (85 ms at 48 kHz)

struct ParameterInfo {
    const char* id;      // in tone files: "drive"
    const char* name;    // shown: "Drive"
    float min, max, standard;
    const char* unit;    // "dB", "ms", "Hz", "%" (0..1 shown as a percentage), "" for none
    const char* description; // what turning it does, for the player
    const char* const* choices = nullptr; // a choice rather than an amount: the names of 0, 1, 2... up to max
};

struct EffectInfo {
    const char* id;          // in tone files: "drive"
    const char* name;        // shown: "Drive"
    const char* description; // a line on what it does
    int parameterCount;
    ParameterInfo parameters[MAX_PARAMETERS];
};
const EffectInfo& effectInfo(EffectType type);

const int MAX_EFFECT_FILE = 128;

struct Effect {
    EffectType type = EffectType::Amp;
    bool on = true;
    float values[MAX_PARAMETERS] = {};
    // A file of the player's it plays through, by name (a cabinet's impulse response in their cabinets folder, a
    // capture's model in their captures folder); "" for none. Kept in place, not as a string, so the audio thread can copy a tone as plain bytes.
    char file[MAX_EFFECT_FILE] = {};
};
void setEffectFile(Effect& effect, const std::string& name); // cut to fit
Effect makeEffect(EffectType type); // with its standard settings

struct Tone {
    std::string name;
    std::vector<Effect> effects; // in the order the sound goes through them, at most MAX_EFFECTS
    float volume = 0.8f;         // after them all, 0..1
};

// Tones that come with lahn, the first the default: a clean one, and a few to start from
const std::vector<Tone>& builtInTones();
const Tone* findBuiltInTone(const std::string& name);

// The text of a tone file, and reading one back. Reading is lenient, like the settings: an unknown effect or setting
// is skipped (a tone from a newer lahn still loads), a missing setting takes its standard value, values are kept in
// range. False, with why, only for text that isn't a tone at all.
const char* const TONE_FILE_EXTENSION = ".tone";
std::string writeTone(const Tone& tone);
bool readTone(const std::string& text, Tone& tone, std::string& error);

// --- Running a tone ------------------------------------------------------------------------------------------

// A real amp or pedal as a model of it (a Neural Amp Modeler capture, audio/capture), run by the audio thread a block
// at a time, in place, never allocating. It remembers what it played (an amp's sound depends on it), so each audio
// thread playing tones has one of its own.
class CaptureModel {
public:
    virtual ~CaptureModel() = default;
    virtual void process(float* samples, int count, int sampleRate) = 0;
};
const int TONE_RUNNERS = 2; // the audio threads that play tones (the engine's, and the direct monitor's)

// What an effect plays through beyond its settings, worked out off the audio thread: a cabinet's impulse response
// (core/cabinets), at each rate a device may run at, back to front (the oldest sample meets the last tap); a
// capture's model (audio/capture), one per audio thread, and how loud it plays. Kept as long as the audio thread may
// play it.
struct ToneAsset {
    struct Response {
        int sampleRate = 0;
        std::vector<float> reversed;
    };
    std::vector<Response> responses;
    const Response* forRate(int sampleRate) const; // the nearest; nullptr for none
    std::unique_ptr<CaptureModel> models[TONE_RUNNERS];
    float loudnessDb = -18.0f; // a capture's output with a typical playing level: brought to -18 dB, as NAM's plugin does
};

// A tone as the audio thread takes it: fixed size, nothing to allocate or free
struct ToneParameters {
    Effect effects[MAX_EFFECTS];
    const ToneAsset* assets[MAX_EFFECTS] = {}; // what each plays through (a cabinet's response), nullptr for nothing
    int count = 0;
    float volume = 0.8f;
};
ToneParameters toneParameters(const Tone& tone);

// One effect's workings: its filters' and envelopes' memory, and its delay lines

struct EffectState {
    EffectType type = EffectType::Count; // what it was last set up as: another effect in the slot starts it afresh
    float values[MAX_PARAMETERS] = {};   // the settings its coefficients were worked out for
    Biquad filters[5];
    float envelope = 0.0f, gain = 1.0f;
    float lowPass = 0.0f;
    float lastSign = 1.0f, flip = 1.0f;  // the octaver's
    float phase = 0.0f;                  // the chorus's
    std::vector<float> line;             // a delay line (delay, chorus)
    int write = 0;
    std::vector<float> combs[8], allpasses[4]; // the reverb's, sized for the highest rate
    int combLength[8] = {}, allpassLength[4] = {}; // the part of each used at the rate now
    int combAt[8] = {}, allpassAt[4] = {};
    float combStore[8] = {};
    AmpState amp;                        // the amp's (core/ampmodel)
    DriveState drive;                    // the drive's
    std::vector<float> history;          // a cabinet's: the input's latest samples, twice over (read in one piece)
    int historyAt = 0;
    const float* taps = nullptr;         //   its response at the rate now, back to front
    int tapCount = 0;
};

struct ToneChain {
    int runner = 0;           // which audio thread runs it: the capture models it plays (ToneAsset::models)
    int sampleRate = 48000;
    ToneParameters parameters;
    EffectState states[MAX_EFFECTS];
    float dcIn = 0.0f, dcOut = 0.0f; // the DC blocker in front of it all
};

// Sizes every delay line for any rate up to 96 kHz (or `sampleRate`, if higher): before the audio thread uses it
// (this allocates)
void initToneChain(ToneChain& chain, int sampleRate);
// A new tone, or new settings: the effects' memory is kept where the effect in a slot is the same kind, so a knob
// turned while playing doesn't click. Allocation-free: safe on the audio thread.
void setToneChain(ToneChain& chain, const ToneParameters& parameters);
void processToneChain(ToneChain& chain, float* samples, int count);
// Silence in every effect's memory, as new (not while an audio thread runs it: it clears whole delay lines)
void clearToneChain(ToneChain& chain);
// Another sample rate (the device changed): the filters are worked out again, the delay lines stay as sized.
// Allocation-free.
void setToneChainRate(ToneChain& chain, int sampleRate);
