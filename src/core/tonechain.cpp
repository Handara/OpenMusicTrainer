#include "core/tonechain.h"

#include "core/cabinets.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <sstream>
#if defined(__SSE__) || defined(_M_X64) || defined(_M_AMD64)
#include <xmmintrin.h>
#define LAHN_SSE 1
#endif

const float PI_F = 3.14159265f;
const float DC_POLE = 0.9995f;          // the DC blocker: a high-pass around 4 Hz at 48 kHz, far under a bass's low E
const float MAX_DELAY_S = 1.6f;         // the delay's longest time, and room for the chorus's short one
const float CHORUS_BASE_S = 0.007f;     // the chorus's copy lags this much...
const float CHORUS_SWING_S = 0.006f;    // ...give or take this, at full depth
const int REVERB_COMBS[8] = { 1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617 }; // Freeverb's tunings, at 44.1 kHz
const int REVERB_ALLPASSES[4] = { 556, 441, 341, 225 };

// --- The effects and their settings -------------------------------------------------------------------------

const EffectInfo& effectInfo(EffectType type){
    static const EffectInfo INFOS[(int)EffectType::Count] = {
        { "gate", "Gate", "Silences the hum and hiss between notes", 2, {
            { "threshold", "Threshold", -90.0f, -20.0f, -65.0f, "dB",
              "Quieter than this counts as silence and is muted. Raise it until the hum between notes is gone, no further" },
            { "release", "Release", 10.0f, 500.0f, 80.0f, "ms",
              "How quickly the sound is muted once you stop: longer lets a note's tail ring out" } } },
        { "compressor", "Compressor", "Evens out loud and soft playing: every note as present", 5, {
            { "threshold", "Threshold", -50.0f, 0.0f, -20.0f, "dB",
              "Louder than this gets turned down. Lower it and more of your playing is evened out" },
            { "ratio", "Ratio", 1.0f, 20.0f, 4.0f, ":1",
              "How hard it turns loud notes down: 2:1 is gentle, 10:1 and up flattens everything" },
            { "attack", "Attack", 0.5f, 50.0f, 5.0f, "ms",
              "How fast it grabs a loud note. Slower lets the pluck's snap through before it steps in" },
            { "release", "Release", 20.0f, 800.0f, 150.0f, "ms",
              "How fast it lets go after a loud note: short pumps, long is smooth" },
            { "makeup", "Makeup", 0.0f, 24.0f, 6.0f, "dB",
              "Turns everything back up after the loud parts were turned down" } } },
        { "drive", "Drive", "A pedal's grit, from warm overdrive to fuzz; blend keeps the clean low end", 5, {
            { "drive", "Drive", 0.0f, 1.0f, 0.5f, "%",
              "How hard the signal is pushed into the clipping: from a touch of warmth to full grit" },
            { "character", "Character", 0.0f, 1.0f, 0.3f, "%",
              "The kind of grit: round and warm on the left, harder in the middle, lopsided fuzz on the right" },
            { "tone", "Tone", 0.0f, 1.0f, 0.6f, "%",
              "Dark to bright: turn it down to tame the fizz" },
            { "level", "Level", 0.0f, 1.0f, 0.6f, "%",
              "How loud it comes out, to match the sound with it off" },
            { "blend", "Blend", 0.0f, 1.0f, 1.0f, "%",
              "How much of the driven sound, against your clean one: less keeps the clean low end under the grit" } } },
        { "amp", "Amp", "An amp and its speaker: gain, bass, mid and treble", 5, {
            { "gain", "Gain", 0.0f, 1.0f, 0.25f, "%",
              "How hard the amp is pushed: clean at the bottom, warm and rounded higher up" },
            { "bass", "Bass", -12.0f, 12.0f, 0.0f, "dB",
              "The low end: the weight and the boom" },
            { "mid", "Mid", -12.0f, 12.0f, 0.0f, "dB",
              "The middle: what makes a note cut through the rest of the band" },
            { "treble", "Treble", -12.0f, 12.0f, 0.0f, "dB",
              "The top: the attack's click and the strings' sparkle" },
            { "cabinet", "Cabinet", 0.0f, 1.0f, 0.6f, "%",
              "How much it sounds through a speaker: none is straight from the pickups, full is a real cabinet's softer top" } } },
        { "equalizer", "Equalizer", "Four bands and a low cut, to shape the sound exactly", 5, {
            { "lowcut", "Low cut", 20.0f, 300.0f, 30.0f, "Hz",
              "Everything below this is cut: clears rumble and mud" },
            { "low", "Low", -15.0f, 15.0f, 0.0f, "dB",
              "Around 100 Hz: the thump and the weight" },
            { "lowmid", "Low mid", -15.0f, 15.0f, 0.0f, "dB",
              "Around 400 Hz: warmth, or mud when there's too much" },
            { "highmid", "High mid", -15.0f, 15.0f, 0.0f, "dB",
              "Around 1.5 kHz: presence, the growl and the bite" },
            { "high", "High", -15.0f, 15.0f, 0.0f, "dB",
              "Around 5 kHz and up: air, string noise and the pick's click" } } },
        { "octaver", "Octaver", "Adds the octave below, the way analog pedals do: no delay", 3, {
            { "sub", "Octave down", 0.0f, 1.0f, 0.6f, "%",
              "How loud the octave below is" },
            { "dry", "Dry", 0.0f, 1.0f, 0.8f, "%",
              "How loud your own note stays beside it" },
            { "tone", "Tone", 0.0f, 1.0f, 0.4f, "%",
              "The octave's color: smooth and round on the left, buzzier on the right" } } },
        { "chorus", "Chorus", "A shimmering second voice beside the first", 3, {
            { "rate", "Rate", 0.1f, 5.0f, 0.8f, "Hz",
              "How fast the shimmer moves" },
            { "depth", "Depth", 0.0f, 1.0f, 0.5f, "%",
              "How far the second voice drifts from the first: more is wider and more seasick" },
            { "mix", "Mix", 0.0f, 1.0f, 0.4f, "%",
              "How loud the second voice is beside yours" } } },
        { "delay", "Delay", "Echoes of what was played, fading", 4, {
            { "time", "Time", 40.0f, 1500.0f, 350.0f, "ms",
              "How long until each echo" },
            { "feedback", "Feedback", 0.0f, 0.9f, 0.35f, "%",
              "How many echoes: each one feeds the next" },
            { "mix", "Mix", 0.0f, 1.0f, 0.25f, "%",
              "How loud the echoes are against what you play" },
            { "tone", "Tone", 0.0f, 1.0f, 0.5f, "%",
              "Each echo darker (left) or as bright as the note (right)" } } },
        { "reverb", "Reverb", "The room it's played in, from a booth to a hall", 3, {
            { "size", "Size", 0.0f, 1.0f, 0.5f, "%",
              "The room's size: a small booth to a big hall" },
            { "damping", "Damping", 0.0f, 1.0f, 0.5f, "%",
              "How much the walls soak up the highs: more is warmer and darker" },
            { "mix", "Mix", 0.0f, 1.0f, 0.2f, "%",
              "How much of the room you hear against your dry sound" } } },
        { "cabinet", "Cabinet", "A speaker cabinet and the mic on it: what makes an amp sound recorded, not fizzy", 5, {
            { "speaker", "Speaker", 0.0f, (float)(cabinetCount() - 1), 2.0f, "",
              "Which cabinet: open-backed and airy, closed and tight, big bass cabinets", cabinetNames() },
            { "mic", "Mic", 0.0f, 1.0f, 0.3f, "%",
              "Where the mic points: at the speaker's middle for bite (left), toward its edge for a rounder, darker sound (right)" },
            { "lowcut", "Low cut", 20.0f, 300.0f, 30.0f, "Hz",
              "Everything below this is cut: tightens a boomy low end" },
            { "highcut", "High cut", 2000.0f, 20000.0f, 12000.0f, "Hz",
              "Everything above this is cut: tames the last of the fizz" },
            { "level", "Level", -12.0f, 12.0f, 0.0f, "dB",
              "How loud it comes out" } } },
    };
    return INFOS[std::clamp((int)type, 0, (int)EffectType::Count - 1)];
}

Effect makeEffect(EffectType type){
    Effect effect;
    effect.type = type;
    const EffectInfo& info = effectInfo(type);
    for (int i = 0; i < info.parameterCount; i++) effect.values[i] = info.parameters[i].standard;
    return effect;
}

// An effect with some of its settings changed from the standard ones, by id
static Effect effectWith(EffectType type, std::initializer_list<std::pair<const char*, float>> values){
    Effect effect = makeEffect(type);
    const EffectInfo& info = effectInfo(type);
    for (const auto& [id, value] : values){
        for (int i = 0; i < info.parameterCount; i++) if (std::string(info.parameters[i].id) == id) effect.values[i] = value;
    }
    return effect;
}

const std::vector<Tone>& builtInTones(){
    static const std::vector<Tone> TONES = {
        // The speaker is a cabinet of its own (core/cabinets), the amp's own speaker filter left out
        { "Clean", {
            effectWith(EffectType::Compressor, {{"threshold", -22.0f}, {"ratio", 3.0f}, {"attack", 8.0f}, {"makeup", 5.0f}}),
            effectWith(EffectType::Amp, {{"gain", 0.15f}, {"bass", 2.0f}, {"treble", 1.0f}, {"cabinet", 0.0f}}),
            effectWith(EffectType::Cabinet, {{"speaker", 5.0f}, {"mic", 0.2f}}), // full range, for a bass or a guitar
            effectWith(EffectType::Reverb, {{"size", 0.35f}, {"mix", 0.12f}}) }, 0.8f },
        { "Warm", {
            effectWith(EffectType::Compressor, {{"threshold", -20.0f}, {"ratio", 4.0f}, {"makeup", 6.0f}}),
            effectWith(EffectType::Amp, {{"gain", 0.45f}, {"bass", 3.0f}, {"mid", 2.0f}, {"treble", -2.0f}, {"cabinet", 0.0f}}),
            effectWith(EffectType::Cabinet, {{"speaker", 3.0f}, {"mic", 0.5f}}) }, 0.8f },
        { "Growl", {
            effectWith(EffectType::Compressor, {{"threshold", -20.0f}, {"makeup", 5.0f}}),
            effectWith(EffectType::Drive, {{"drive", 0.55f}, {"character", 0.4f}, {"tone", 0.55f}, {"level", 0.6f}, {"blend", 0.55f}}),
            effectWith(EffectType::Amp, {{"gain", 0.3f}, {"bass", 2.0f}, {"mid", 3.0f}, {"cabinet", 0.0f}}),
            effectWith(EffectType::Cabinet, {{"speaker", 4.0f}, {"mic", 0.3f}}) }, 0.75f },
        { "Fuzz", {
            effectWith(EffectType::Drive, {{"drive", 0.9f}, {"character", 1.0f}, {"tone", 0.45f}, {"level", 0.5f}, {"blend", 0.8f}}),
            effectWith(EffectType::Amp, {{"gain", 0.2f}, {"cabinet", 0.0f}}),
            effectWith(EffectType::Cabinet, {{"speaker", 2.0f}, {"mic", 0.3f}, {"highcut", 9000.0f}}) }, 0.7f },
        { "Dub", {
            effectWith(EffectType::Octaver, {{"sub", 0.7f}, {"dry", 0.7f}, {"tone", 0.3f}}),
            effectWith(EffectType::Compressor, {{"threshold", -24.0f}, {"makeup", 6.0f}}),
            effectWith(EffectType::Equalizer, {{"low", 4.0f}, {"highmid", -3.0f}, {"high", -6.0f}}),
            effectWith(EffectType::Cabinet, {{"speaker", 6.0f}, {"mic", 0.4f}}),
            effectWith(EffectType::Reverb, {{"size", 0.5f}, {"damping", 0.6f}, {"mix", 0.18f}}) }, 0.75f },
        { "Space", {
            effectWith(EffectType::Compressor, {{"makeup", 4.0f}}),
            effectWith(EffectType::Cabinet, {{"speaker", 1.0f}, {"mic", 0.3f}}),
            effectWith(EffectType::Chorus, {{"rate", 0.6f}, {"depth", 0.5f}, {"mix", 0.45f}}),
            effectWith(EffectType::Delay, {{"time", 380.0f}, {"feedback", 0.35f}, {"mix", 0.25f}}),
            effectWith(EffectType::Reverb, {{"size", 0.75f}, {"damping", 0.4f}, {"mix", 0.3f}}) }, 0.75f },
    };
    return TONES;
}

const Tone* findBuiltInTone(const std::string& name){
    for (const Tone& tone : builtInTones()) if (tone.name == name) return &tone;
    return nullptr;
}

// --- Tone files ---------------------------------------------------------------------------------------------

const int TONE_FILE_VERSION = 1;

std::string writeTone(const Tone& tone){
    std::ostringstream out;
    out << "# a lahn tone: the effects the instrument goes through, in order\n";
    out << "lahn_tone " << TONE_FILE_VERSION << "\n";
    out << "name " << tone.name << "\n";
    out << "volume " << tone.volume << "\n";
    for (const Effect& effect : tone.effects){
        const EffectInfo& info = effectInfo(effect.type);
        out << info.id << " " << (effect.on ? "on" : "off");
        for (int i = 0; i < info.parameterCount; i++) out << " " << info.parameters[i].id << " " << effect.values[i];
        out << "\n";
    }
    return out.str();
}

bool readTone(const std::string& text, Tone& tone, std::string& error){
    std::istringstream lines(text);
    std::string line;
    bool started = false;
    Tone read;
    while (std::getline(lines, line)){
        if (!line.empty() && line.back() == '\r') line.pop_back();
        std::istringstream words(line);
        std::string key;
        if (!(words >> key) || key[0] == '#') continue;
        if (!started){
            if (key != "lahn_tone"){
                error = "not a lahn tone";
                return false;
            }
            started = true;
            continue;
        }
        if (key == "name"){
            std::string name;
            std::getline(words >> std::ws, name);
            read.name = name;
            continue;
        }
        if (key == "volume"){
            float volume;
            if (words >> volume) read.volume = std::clamp(volume, 0.0f, 1.0f);
            continue;
        }
        // An effect: its id, on or off, then its settings as pairs
        int type = -1;
        for (int t = 0; t < (int)EffectType::Count; t++) if (key == effectInfo((EffectType)t).id) type = t;
        if (type < 0 || (int)read.effects.size() >= MAX_EFFECTS) continue;
        Effect effect = makeEffect((EffectType)type);
        std::string state;
        if (words >> state) effect.on = state != "off";
        const EffectInfo& info = effectInfo(effect.type);
        std::string id;
        float value;
        while (words >> id >> value){
            for (int i = 0; i < info.parameterCount; i++){
                if (id == info.parameters[i].id) effect.values[i] = std::clamp(value, info.parameters[i].min, info.parameters[i].max);
            }
        }
        read.effects.push_back(effect);
    }
    if (!started){
        error = "not a lahn tone";
        return false;
    }
    tone = read;
    return true;
}

// --- Running a tone -----------------------------------------------------------------------------------------

const ToneAsset::Response* ToneAsset::forRate(int sampleRate) const {
    const Response* nearest = nullptr;
    for (const Response& response : responses)
        if (!nearest || std::abs(response.sampleRate - sampleRate) < std::abs(nearest->sampleRate - sampleRate)) nearest = &response;
    return nearest;
}

ToneParameters toneParameters(const Tone& tone){
    ToneParameters parameters;
    parameters.count = std::min((int)tone.effects.size(), MAX_EFFECTS);
    for (int i = 0; i < parameters.count; i++) parameters.effects[i] = tone.effects[i];
    parameters.volume = std::clamp(tone.volume, 0.0f, 1.0f);
    return parameters;
}

const int CAPACITY_RATE = 96000; // delay lines are sized for this rate, so another device never needs new memory

// The reverb's lines, the length Freeverb's tunings have at this rate
static void setReverbLengths(EffectState& state, int sampleRate){
    const float scale = sampleRate / 44100.0f;
    for (int c = 0; c < 8; c++){
        state.combLength[c] = std::clamp((int)(REVERB_COMBS[c] * scale), 1, (int)state.combs[c].size());
        state.combAt[c] %= state.combLength[c];
    }
    for (int a = 0; a < 4; a++){
        state.allpassLength[a] = std::clamp((int)(REVERB_ALLPASSES[a] * scale), 1, (int)state.allpasses[a].size());
        state.allpassAt[a] %= state.allpassLength[a];
    }
}

void initToneChain(ToneChain& chain, int sampleRate){
    chain.sampleRate = std::max(8000, sampleRate);
    const int capacity = std::max(CAPACITY_RATE, chain.sampleRate);
    const float scale = capacity / 44100.0f;
    for (EffectState& state : chain.states){
        state = EffectState{};
        state.line.assign((size_t)(MAX_DELAY_S * capacity) + 4, 0.0f);
        for (int c = 0; c < 8; c++) state.combs[c].assign((size_t)(REVERB_COMBS[c] * scale) + 1, 0.0f);
        for (int a = 0; a < 4; a++) state.allpasses[a].assign((size_t)(REVERB_ALLPASSES[a] * scale) + 1, 0.0f);
        setReverbLengths(state, chain.sampleRate);
        state.history.assign(2 * (size_t)MAX_CABINET_TAPS, 0.0f);
    }
    chain.parameters = ToneParameters{};
    chain.dcIn = chain.dcOut = 0.0f;
}

// RBJ's cookbook filters
static void lowPass(Biquad& f, float frequency, float q, float rate){
    float w = 2.0f * PI_F * std::min(frequency, rate * 0.45f) / rate, c = std::cos(w), alpha = std::sin(w) / (2.0f * q);
    float a0 = 1.0f + alpha;
    f.b0 = (1.0f - c) / 2.0f / a0; f.b1 = (1.0f - c) / a0; f.b2 = f.b0;
    f.a1 = -2.0f * c / a0; f.a2 = (1.0f - alpha) / a0;
}

static void highPass(Biquad& f, float frequency, float q, float rate){
    float w = 2.0f * PI_F * std::min(frequency, rate * 0.45f) / rate, c = std::cos(w), alpha = std::sin(w) / (2.0f * q);
    float a0 = 1.0f + alpha;
    f.b0 = (1.0f + c) / 2.0f / a0; f.b1 = -(1.0f + c) / a0; f.b2 = f.b0;
    f.a1 = -2.0f * c / a0; f.a2 = (1.0f - alpha) / a0;
}

static void peak(Biquad& f, float frequency, float q, float gainDb, float rate){
    float A = std::pow(10.0f, gainDb / 40.0f), w = 2.0f * PI_F * std::min(frequency, rate * 0.45f) / rate;
    float c = std::cos(w), alpha = std::sin(w) / (2.0f * q), a0 = 1.0f + alpha / A;
    f.b0 = (1.0f + alpha * A) / a0; f.b1 = -2.0f * c / a0; f.b2 = (1.0f - alpha * A) / a0;
    f.a1 = -2.0f * c / a0; f.a2 = (1.0f - alpha / A) / a0;
}

static void shelf(Biquad& f, float frequency, float gainDb, bool high, float rate){
    float A = std::pow(10.0f, gainDb / 40.0f), w = 2.0f * PI_F * std::min(frequency, rate * 0.45f) / rate;
    float c = std::cos(w), s = std::sin(w), alpha = s / 2.0f * std::sqrt(2.0f), root = 2.0f * std::sqrt(A) * alpha;
    if (high){
        float a0 = (A + 1) - (A - 1) * c + root;
        f.b0 = A * ((A + 1) + (A - 1) * c + root) / a0; f.b1 = -2 * A * ((A - 1) + (A + 1) * c) / a0; f.b2 = A * ((A + 1) + (A - 1) * c - root) / a0;
        f.a1 = 2 * ((A - 1) - (A + 1) * c) / a0; f.a2 = ((A + 1) - (A - 1) * c - root) / a0;
    } else {
        float a0 = (A + 1) + (A - 1) * c + root;
        f.b0 = A * ((A + 1) - (A - 1) * c + root) / a0; f.b1 = 2 * A * ((A - 1) - (A + 1) * c) / a0; f.b2 = A * ((A + 1) - (A - 1) * c - root) / a0;
        f.a1 = -2 * ((A - 1) + (A + 1) * c) / a0; f.a2 = ((A + 1) + (A - 1) * c - root) / a0;
    }
}

// Works out an effect's filters for its settings (keeping the filters' memory, so nothing clicks)
static void setUp(EffectState& state, const Effect& effect, float rate){
    const float* v = effect.values;
    switch (effect.type){
        case EffectType::Drive:
            highPass(state.filters[0], 60.0f, 0.7f, rate);                                    // no mud into the clipping
            lowPass(state.filters[1], 700.0f * std::pow(12.0f, v[2]), 0.7f, rate);             // tone: 700 Hz to 8.4 kHz
            break;
        case EffectType::Amp:
            shelf(state.filters[0], 120.0f, v[1], false, rate);
            peak(state.filters[1], 600.0f, 0.7f, v[2], rate);
            shelf(state.filters[2], 2500.0f, v[3], true, rate);
            lowPass(state.filters[3], 18000.0f * std::pow(4500.0f / 18000.0f, v[4]), 0.9f + 0.4f * v[4], rate); // the speaker
            highPass(state.filters[4], 35.0f + 30.0f * v[4], 0.7f, rate);
            break;
        case EffectType::Equalizer:
            highPass(state.filters[0], v[0], 0.7f, rate);
            shelf(state.filters[1], 100.0f, v[1], false, rate);
            peak(state.filters[2], 400.0f, 1.0f, v[2], rate);
            peak(state.filters[3], 1500.0f, 1.0f, v[3], rate);
            shelf(state.filters[4], 5000.0f, v[4], true, rate);
            break;
        case EffectType::Octaver:
            lowPass(state.filters[0], 250.0f, 0.7f, rate); // the fundamental alone, for the zero crossings
            lowPass(state.filters[1], 250.0f, 0.7f, rate);
            lowPass(state.filters[2], 150.0f * std::pow(10.0f, v[2]), 0.7f, rate); // the square wave, rounded off
            break;
        case EffectType::Delay:
            lowPass(state.filters[0], 1000.0f * std::pow(12.0f, v[3]), 0.7f, rate); // each echo a little darker
            break;
        case EffectType::Cabinet:
            shelf(state.filters[0], 2500.0f, 3.0f - 11.0f * v[1], true, rate); // the mic: at the middle bright, toward the edge dark...
            peak(state.filters[1], 350.0f, 0.8f, 2.5f * v[1], rate);           // ...and fuller
            highPass(state.filters[2], v[2], 0.7f, rate);
            lowPass(state.filters[3], v[3], 0.7f, rate);
            break;
        default: break;
    }
    for (int i = 0; i < MAX_PARAMETERS; i++) state.values[i] = v[i];
}

// A cabinet's response at the chain's rate, from what it was given to play through. Another length starts its
// history afresh (allocation-free: the history is sized for the longest).
static void takeResponse(EffectState& state, const ToneAsset* asset, int sampleRate){
    const ToneAsset::Response* response = asset ? asset->forRate(sampleRate) : nullptr;
    const int count = response ? std::min((int)response->reversed.size(), MAX_CABINET_TAPS) : 0;
    if (count != state.tapCount){
        std::fill(state.history.begin(), state.history.end(), 0.0f);
        state.historyAt = 0;
    }
    state.taps = count > 0 ? response->reversed.data() : nullptr;
    state.tapCount = count;
}

void setToneChain(ToneChain& chain, const ToneParameters& parameters){
    chain.parameters = parameters;
    for (int i = 0; i < parameters.count; i++){
        EffectState& state = chain.states[i];
        const Effect& effect = parameters.effects[i];
        bool changed = state.type != effect.type;
        if (changed){
            // Another effect in this slot: its memory starts afresh (the buffers stay, emptied)
            for (Biquad& f : state.filters) f.z1 = f.z2 = 0.0f;
            state.envelope = 0.0f;
            state.gain = 1.0f;
            state.lowPass = 0.0f;
            state.lastSign = state.flip = 1.0f;
            state.phase = 0.0f;
            std::fill(state.line.begin(), state.line.end(), 0.0f);
            for (auto& comb : state.combs) std::fill(comb.begin(), comb.end(), 0.0f);
            for (auto& allpass : state.allpasses) std::fill(allpass.begin(), allpass.end(), 0.0f);
            for (float& store : state.combStore) store = 0.0f;
            std::fill(state.history.begin(), state.history.end(), 0.0f);
            state.historyAt = 0;
            state.type = effect.type;
        }
        takeResponse(state, effect.type == EffectType::Cabinet ? parameters.assets[i] : nullptr, chain.sampleRate);
        bool same = !changed;
        for (int p = 0; p < MAX_PARAMETERS && same; p++) same = state.values[p] == effect.values[p];
        if (!same) setUp(state, effect, (float)chain.sampleRate);
    }
}

void clearToneChain(ToneChain& chain){
    for (EffectState& state : chain.states){
        for (Biquad& f : state.filters) f.z1 = f.z2 = 0.0f;
        state.envelope = 0.0f;
        state.gain = 1.0f;
        state.lowPass = 0.0f;
        state.lastSign = state.flip = 1.0f;
        state.phase = 0.0f;
        std::fill(state.line.begin(), state.line.end(), 0.0f);
        for (auto& comb : state.combs) std::fill(comb.begin(), comb.end(), 0.0f);
        for (auto& allpass : state.allpasses) std::fill(allpass.begin(), allpass.end(), 0.0f);
        for (float& store : state.combStore) store = 0.0f;
        std::fill(state.history.begin(), state.history.end(), 0.0f);
        state.historyAt = 0;
    }
    chain.dcIn = chain.dcOut = 0.0f;
}

void setToneChainRate(ToneChain& chain, int sampleRate){
    if (sampleRate <= 0 || sampleRate == chain.sampleRate) return;
    chain.sampleRate = sampleRate;
    for (EffectState& state : chain.states) setReverbLengths(state, sampleRate);
    for (int i = 0; i < chain.parameters.count; i++){
        setUp(chain.states[i], chain.parameters.effects[i], (float)sampleRate);
        if (chain.parameters.effects[i].type == EffectType::Cabinet) takeResponse(chain.states[i], chain.parameters.assets[i], sampleRate);
    }
}

static float dbToGain(float db){ return std::pow(10.0f, db / 20.0f); }

// The sum of the products of two runs of floats: a cabinet's every sample, so four at a time where the processor can
static float dotProduct(const float* a, const float* b, int count){
    int i = 0;
    float sum = 0.0f;
#ifdef LAHN_SSE
    __m128 first = _mm_setzero_ps(), second = _mm_setzero_ps();
    for (; i + 8 <= count; i += 8){
        first = _mm_add_ps(first, _mm_mul_ps(_mm_loadu_ps(a + i), _mm_loadu_ps(b + i)));
        second = _mm_add_ps(second, _mm_mul_ps(_mm_loadu_ps(a + i + 4), _mm_loadu_ps(b + i + 4)));
    }
    float lanes[4];
    _mm_storeu_ps(lanes, _mm_add_ps(first, second));
    sum = lanes[0] + lanes[1] + lanes[2] + lanes[3];
#endif
    for (; i < count; i++) sum += a[i] * b[i];
    return sum;
}
static float quiet(float x){ return std::fabs(x) < 1e-15f ? 0.0f : x; } // no denormals in feedback: they're slow

// One sample through one effect
static float process(EffectState& state, const Effect& effect, float x, float rate){
    const float* v = effect.values;
    switch (effect.type){
        case EffectType::Gate: {
            float level = std::fabs(x);
            float fall = std::exp(-1.0f / (0.005f * rate));
            state.envelope = std::max(level, state.envelope * fall);
            bool open = state.envelope > dbToGain(v[0]);
            float target = open ? 1.0f : 0.0f;
            float speed = open ? 1.0f - std::exp(-1.0f / (0.001f * rate)) : 1.0f - std::exp(-1.0f / (v[1] * 0.001f * rate));
            state.gain += (target - state.gain) * speed;
            return x * state.gain;
        }
        case EffectType::Compressor: {
            float level = std::fabs(x);
            float coefficient = level > state.envelope ? 1.0f - std::exp(-1.0f / (v[2] * 0.001f * rate))
                                                       : 1.0f - std::exp(-1.0f / (v[3] * 0.001f * rate));
            state.envelope += (level - state.envelope) * coefficient;
            float levelDb = 20.0f * std::log10(std::max(state.envelope, 1e-6f));
            float over = std::max(0.0f, levelDb - v[0]);
            return x * dbToGain(-over * (1.0f - 1.0f / std::max(1.0f, v[1])) + v[4]);
        }
        case EffectType::Drive: {
            float gain = 1.0f + v[0] * v[0] * 80.0f;
            float pushed = state.filters[0].process(x) * gain;
            // The character: from a soft, round clip toward a hard one, then lopsided, like a fuzz
            float hardness = 2.0f + 8.0f * v[1];
            float bias = std::max(0.0f, v[1] - 0.6f) * 0.5f;
            float shaped = (pushed + bias) / std::pow(1.0f + std::pow(std::fabs(pushed + bias), hardness), 1.0f / hardness) - bias / std::pow(1.0f + std::pow(bias, hardness), 1.0f / hardness);
            float wet = state.filters[1].process(shaped) * v[3] * 1.4f;
            return wet * v[4] + x * (1.0f - v[4]);
        }
        case EffectType::Amp: {
            float gain = 1.0f + v[0] * 24.0f;
            float driven = std::tanh(gain * x) / std::tanh(gain) * (1.0f / (1.0f + v[0])); // pushed, rounded, not louder
            float y = driven;
            for (int f = 0; f < 5; f++) y = state.filters[f].process(y);
            return y;
        }
        case EffectType::Equalizer: {
            float y = x;
            for (int f = 0; f < 5; f++) y = state.filters[f].process(y);
            return y;
        }
        case EffectType::Octaver: {
            // The fundamental's zero crossings flip a square wave at half its frequency; it follows the playing's
            // loudness, and is rounded off into a warm sub-octave
            float fundamental = state.filters[1].process(state.filters[0].process(x));
            float sign = fundamental > 0.0005f ? 1.0f : fundamental < -0.0005f ? -1.0f : state.lastSign;
            if (sign > 0.0f && state.lastSign < 0.0f) state.flip = -state.flip;
            state.lastSign = sign;
            float level = std::fabs(x);
            state.envelope += (level - state.envelope) * (level > state.envelope ? 0.01f : 0.0015f);
            float sub = state.filters[2].process(state.flip * state.envelope * 1.6f);
            return x * v[1] + sub * v[0];
        }
        case EffectType::Chorus: {
            const int size = (int)state.line.size();
            state.line[state.write] = x;
            state.phase += 2.0f * PI_F * v[0] / rate;
            if (state.phase > 2.0f * PI_F) state.phase -= 2.0f * PI_F;
            float lag = (CHORUS_BASE_S + CHORUS_SWING_S * v[1] * 0.5f * (1.0f + std::sin(state.phase))) * rate;
            float at = state.write - lag;
            while (at < 0.0f) at += size;
            int a = (int)at, b = (a + 1) % size;
            float fraction = at - a;
            float wet = state.line[a] + (state.line[b] - state.line[a]) * fraction;
            state.write = (state.write + 1) % size;
            return (x + v[2] * wet) / (1.0f + 0.5f * v[2]);
        }
        case EffectType::Delay: {
            const int size = (int)state.line.size();
            int lag = std::clamp((int)(v[0] * 0.001f * rate), 1, size - 1);
            int at = state.write - lag;
            if (at < 0) at += size;
            float echo = state.line[at];
            state.line[state.write] = quiet(x + state.filters[0].process(echo) * v[1]);
            state.write = (state.write + 1) % size;
            return x + echo * v[2];
        }
        case EffectType::Cabinet: {
            // Convolution with its response, sample by sample: nothing waits for a block, so it adds no delay
            float y = x;
            if (state.taps){
                const int n = state.tapCount;
                float* history = state.history.data();
                history[state.historyAt] = history[state.historyAt + n] = x;
                y = dotProduct(history + state.historyAt + 1, state.taps, n); // the latest n, oldest first
                state.historyAt = (state.historyAt + 1) % n;
            }
            for (int f = 0; f < 4; f++) y = state.filters[f].process(y);
            return y * dbToGain(v[4]);
        }
        case EffectType::Reverb: {
            // Freeverb: eight damped combs side by side, then four all-passes in a row
            float feedback = 0.7f + 0.28f * v[0], damp = 0.05f + 0.4f * v[1];
            float input = x * 0.015f, wet = 0.0f;
            for (int c = 0; c < 8; c++){
                std::vector<float>& comb = state.combs[c];
                int& at = state.combAt[c];
                float out = comb[at];
                state.combStore[c] = quiet(out * (1.0f - damp) + state.combStore[c] * damp);
                comb[at] = input + state.combStore[c] * feedback;
                at = (at + 1) % state.combLength[c];
                wet += out;
            }
            for (int a = 0; a < 4; a++){
                std::vector<float>& allpass = state.allpasses[a];
                int& at = state.allpassAt[a];
                float held = allpass[at];
                allpass[at] = quiet(wet + held * 0.5f);
                wet = held - wet;
                at = (at + 1) % state.allpassLength[a];
            }
            return x * (1.0f - 0.3f * v[2]) + wet * 3.0f * v[2];
        }
        default: return x;
    }
}

void processToneChain(ToneChain& chain, float* samples, int count){
    const ToneParameters& parameters = chain.parameters;
    const float rate = (float)chain.sampleRate;
    for (int i = 0; i < count; i++){
        float x = samples[i];
        float blocked = x - chain.dcIn + DC_POLE * chain.dcOut; // no DC: it would only eat into the headroom
        chain.dcIn = x;
        chain.dcOut = quiet(blocked);
        float y = blocked;
        for (int e = 0; e < parameters.count; e++){
            if (parameters.effects[e].on) y = process(chain.states[e], parameters.effects[e], y, rate);
        }
        y *= parameters.volume;
        samples[i] = std::clamp(std::isfinite(y) ? y : 0.0f, -1.0f, 1.0f); // never past full scale, whatever the tone
    }
}
