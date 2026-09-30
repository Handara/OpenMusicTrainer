#pragma once

#include <string>
#include <vector>

// A tone: the instrument's own sound shaped by a chain of effects, in order, like pedals into an amp or the effects
// on a DAW's track. Every effect works sample by sample and never looks ahead, so the chain adds no delay: the
// instrument is heard the moment it's played, whatever the tone. Tones are kept as small text files, to save, share
// and load. Pure; the audio thread runs a chain without allocating.

enum class EffectType { Gate, Compressor, Drive, Amp, Equalizer, Octaver, Chorus, Delay, Reverb, Count };

const int MAX_PARAMETERS = 6;
const int MAX_EFFECTS = 12;

struct ParameterInfo {
    const char* id;      // in tone files: "drive"
    const char* name;    // shown: "Drive"
    float min, max, standard;
    const char* unit;    // "dB", "ms", "Hz", "%" (0..1 shown as a percentage), "" for none
    const char* description; // what turning it does, for the player
};

struct EffectInfo {
    const char* id;          // in tone files: "drive"
    const char* name;        // shown: "Drive"
    const char* description; // a line on what it does
    int parameterCount;
    ParameterInfo parameters[MAX_PARAMETERS];
};
const EffectInfo& effectInfo(EffectType type);

struct Effect {
    EffectType type = EffectType::Amp;
    bool on = true;
    float values[MAX_PARAMETERS] = {};
};
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

// A tone as the audio thread takes it: fixed size, nothing to allocate or free
struct ToneParameters {
    Effect effects[MAX_EFFECTS];
    int count = 0;
    float volume = 0.8f;
};
ToneParameters toneParameters(const Tone& tone);

// One effect's workings: its filters' and envelopes' memory, and its delay lines
struct Biquad {
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    float z1 = 0, z2 = 0;
    float process(float x){
        float y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
};

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
};

struct ToneChain {
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
