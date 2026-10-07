#include "core/tonechain.h"

#include "core/cabinets.h"
#include "core/filters.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <sstream>
#if defined(__SSE__) || defined(_M_X64) || defined(_M_AMD64)
#include <xmmintrin.h>
#define HARDTHZ_SSE 1
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
        { "drive", "Drive", "A pedal's grit: an overdrive, a distortion or a fuzz, modelled on the classics", 5, {
            { "drive", "Drive", 0.0f, 1.0f, 0.5f, "%",
              "How hard the signal is pushed into the clipping: from a touch of warmth to full grit" },
            { "type", "Type", 0.0f, 2.0f, 0.0f, "",
              "Overdrive: smooth, mids forward, lows clean (a Tube Screamer). Distortion: harder and thicker (a RAT). Fuzz: "
              "a wall of sound, mids scooped (a Big Muff)", driveTypeNames() },
            { "tone", "Tone", 0.0f, 1.0f, 0.6f, "%",
              "Dark to bright: turn it down to tame the fizz" },
            { "level", "Level", 0.0f, 1.0f, 0.6f, "%",
              "How loud it comes out, to match the sound with it off" },
            { "blend", "Blend", 0.0f, 1.0f, 1.0f, "%",
              "How much of the driven sound, against your clean one: less keeps the clean low end under the grit" } } },
        { "amp", "Amp", "A tube amp, modelled on a classic: its gain stages, its own tone stack, its power amp", 6, {
            { "model", "Model", 0.0f, (float)(ampModelCount() - 1), 1.0f, "",
              "Which amp: clean American, British crunch and lead, modern high gain, a tube bass amp, a bass drive", ampModelNames() },
            { "gain", "Gain", 0.0f, 1.0f, 0.4f, "%",
              "How hard its tubes are pushed: clean at the bottom, breaking up, then full distortion" },
            { "bass", "Bass", 0.0f, 1.0f, 0.5f, "%",
              "The low end: the weight and the boom. The knobs work as the amp's own do, each changing the others a little" },
            { "mid", "Mid", 0.0f, 1.0f, 0.5f, "%",
              "The middle: what makes a note cut through the rest of the band" },
            { "treble", "Treble", 0.0f, 1.0f, 0.5f, "%",
              "The top: the attack's click and the strings' sparkle" },
            { "presence", "Presence", 0.0f, 1.0f, 0.5f, "%",
              "The power amp's bite, above the treble: how it cuts through" } } },
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
        { "capture", "Capture", "A real amp or pedal, captured with Neural Amp Modeler: drop a .nam file from TONE3000 here", 2, {
            { "input", "Input", -24.0f, 24.0f, 0.0f, "dB",
              "How hard you hit it: up for more of the amp's grit, down for cleaner" },
            { "level", "Level", -24.0f, 24.0f, 0.0f, "dB",
              "How loud it comes out (every capture starts as loud as the others)" } } },
    };
    return INFOS[std::clamp((int)type, 0, (int)EffectType::Count - 1)];
}

void setEffectFile(Effect& effect, const std::string& name){
    const size_t length = std::min(name.size(), (size_t)MAX_EFFECT_FILE - 1);
    std::memcpy(effect.file, name.data(), length);
    effect.file[length] = '\0';
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

// The amp models (core/ampmodel), by their place in its list
const float CLEAN_US = 0.0f, CRUNCH_UK = 1.0f, LEAD_UK = 2.0f, MODERN = 3.0f, BASS_TUBE = 4.0f, BASS_DRIVE = 5.0f;
// The cabinets (core/cabinets)
const float OPEN_1X12 = 0.0f, ALNICO_2X12 = 1.0f, MODERN_4X12 = 2.0f, VINTAGE_4X12 = 3.0f, BASS_8X10 = 4.0f, BASS_4X10 = 5.0f, BASS_1X15 = 6.0f;

const std::vector<Tone>& builtInTones(){
    static const std::vector<Tone> TONES = {
        // For either instrument: a clean amp into a full-range cabinet
        { "Clean", {
            effectWith(EffectType::Compressor, {{"threshold", -22.0f}, {"ratio", 3.0f}, {"attack", 8.0f}, {"makeup", 5.0f}}),
            effectWith(EffectType::Amp, {{"model", CLEAN_US}, {"gain", 0.2f}, {"bass", 0.55f}, {"mid", 0.5f}, {"treble", 0.55f}, {"presence", 0.45f}}),
            effectWith(EffectType::Cabinet, {{"speaker", BASS_4X10}, {"mic", 0.25f}}),
            effectWith(EffectType::Reverb, {{"size", 0.35f}, {"mix", 0.12f}}) }, 0.8f },
        // A '59 Bassman into its 4x10, just breaking up: warm for a bass, bluesy for a guitar
        { "Warm", {
            effectWith(EffectType::Compressor, {{"threshold", -20.0f}, {"ratio", 4.0f}, {"makeup", 6.0f}}),
            effectWith(EffectType::Amp, {{"model", BASS_TUBE}, {"gain", 0.45f}, {"bass", 0.6f}, {"mid", 0.6f}, {"treble", 0.4f}, {"presence", 0.4f}}),
            effectWith(EffectType::Cabinet, {{"speaker", BASS_4X10}, {"mic", 0.5f}}) }, 0.8f },
        // Guitar
        { "Crunch", {
            effectWith(EffectType::Gate, {{"threshold", -70.0f}}),
            effectWith(EffectType::Amp, {{"model", CRUNCH_UK}, {"gain", 0.55f}, {"bass", 0.5f}, {"mid", 0.65f}, {"treble", 0.55f}, {"presence", 0.5f}}),
            effectWith(EffectType::Cabinet, {{"speaker", VINTAGE_4X12}, {"mic", 0.3f}}),
            effectWith(EffectType::Reverb, {{"size", 0.3f}, {"mix", 0.1f}}) }, 0.75f },
        { "Lead", {
            effectWith(EffectType::Gate, {{"threshold", -62.0f}}),
            effectWith(EffectType::Drive, {{"drive", 0.2f}, {"type", 0.0f}, {"tone", 0.6f}, {"level", 0.8f}, {"blend", 1.0f}}), // a boost, tightening
            effectWith(EffectType::Amp, {{"model", LEAD_UK}, {"gain", 0.7f}, {"bass", 0.45f}, {"mid", 0.7f}, {"treble", 0.55f}, {"presence", 0.55f}}),
            effectWith(EffectType::Cabinet, {{"speaker", MODERN_4X12}, {"mic", 0.35f}}),
            effectWith(EffectType::Delay, {{"time", 420.0f}, {"feedback", 0.25f}, {"mix", 0.15f}}),
            effectWith(EffectType::Reverb, {{"size", 0.5f}, {"mix", 0.15f}}) }, 0.7f },
        { "Modern", {
            effectWith(EffectType::Gate, {{"threshold", -58.0f}, {"release", 40.0f}}),
            effectWith(EffectType::Amp, {{"model", MODERN}, {"gain", 0.65f}, {"bass", 0.55f}, {"mid", 0.4f}, {"treble", 0.6f}, {"presence", 0.55f}}),
            effectWith(EffectType::Cabinet, {{"speaker", MODERN_4X12}, {"mic", 0.25f}, {"lowcut", 70.0f}}) }, 0.7f },
        // Bass
        { "Growl", {
            effectWith(EffectType::Compressor, {{"threshold", -20.0f}, {"makeup", 5.0f}}),
            effectWith(EffectType::Amp, {{"model", BASS_DRIVE}, {"gain", 0.5f}, {"bass", 0.55f}, {"mid", 0.6f}, {"treble", 0.5f}, {"presence", 0.5f}}),
            effectWith(EffectType::Cabinet, {{"speaker", BASS_8X10}, {"mic", 0.3f}}) }, 0.75f },
        { "Fuzz", {
            effectWith(EffectType::Drive, {{"drive", 0.75f}, {"type", 2.0f}, {"tone", 0.45f}, {"level", 0.55f}, {"blend", 0.85f}}),
            effectWith(EffectType::Amp, {{"model", CLEAN_US}, {"gain", 0.3f}, {"bass", 0.5f}, {"mid", 0.55f}, {"treble", 0.5f}}),
            effectWith(EffectType::Cabinet, {{"speaker", MODERN_4X12}, {"mic", 0.3f}, {"highcut", 9000.0f}}) }, 0.7f },
        { "Dub", {
            effectWith(EffectType::Octaver, {{"sub", 0.7f}, {"dry", 0.7f}, {"tone", 0.3f}}),
            effectWith(EffectType::Compressor, {{"threshold", -24.0f}, {"makeup", 6.0f}}),
            effectWith(EffectType::Equalizer, {{"low", 4.0f}, {"highmid", -3.0f}, {"high", -6.0f}}),
            effectWith(EffectType::Cabinet, {{"speaker", BASS_1X15}, {"mic", 0.4f}}),
            effectWith(EffectType::Reverb, {{"size", 0.5f}, {"damping", 0.6f}, {"mix", 0.18f}}) }, 0.75f },
        // Either: clean and wide
        { "Space", {
            effectWith(EffectType::Compressor, {{"makeup", 4.0f}}),
            effectWith(EffectType::Amp, {{"model", CLEAN_US}, {"gain", 0.15f}, {"bass", 0.5f}, {"mid", 0.45f}, {"treble", 0.6f}}),
            effectWith(EffectType::Cabinet, {{"speaker", ALNICO_2X12}, {"mic", 0.3f}}),
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

const int TONE_FILE_VERSION = 2; // 2: the amp modelled on real ones (its knobs as theirs), the drive's types

std::string writeTone(const Tone& tone){
    std::ostringstream out;
    out << "# a hardthz tone: the effects the instrument goes through, in order\n";
    out << "hardthz_tone " << TONE_FILE_VERSION << "\n";
    out << "name " << tone.name << "\n";
    out << "volume " << tone.volume << "\n";
    for (const Effect& effect : tone.effects){
        const EffectInfo& info = effectInfo(effect.type);
        out << info.id << " " << (effect.on ? "on" : "off");
        for (int i = 0; i < info.parameterCount; i++) out << " " << info.parameters[i].id << " " << effect.values[i];
        if (effect.file[0]) out << " file " << effect.file; // last: the rest of the line is its name
        out << "\n";
    }
    return out.str();
}

// A tone from before version 2: its amp's bass, mid and treble were in dB (now the amp's own pots, 0 to 1), its gain
// chose no model (a gentle one is, by how hard it was pushed); its drive's character (a continuous knob) is now a type
static void upgradeEffect(Effect& effect, std::vector<std::pair<std::string, float>>& settings, const std::vector<Effect>&){
    for (auto& [id, value] : settings){
        if (effect.type == EffectType::Amp && (id == "bass" || id == "mid" || id == "treble")) value = 0.5f + value / 24.0f;
        if (effect.type == EffectType::Drive && id == "character"){
            id = "type";
            value = value < 0.45f ? 0.0f : value < 0.75f ? 1.0f : 2.0f;
        }
    }
    if (effect.type == EffectType::Amp){
        float gain = 0.25f;
        for (const auto& [id, value] : settings) if (id == "gain") gain = value;
        settings.push_back({ "model", gain < 0.35f ? 0.0f : 1.0f }); // clean, or the crunch
    }
}

// ...and its amp had a speaker of its own (how much of it, 0 to 1): a cabinet now, after the amp, when it was used
// and the tone has none
static void upgradeAmpSpeaker(float speaker, int ampAt, std::vector<Effect>& effects){
    for (const Effect& effect : effects) if (effect.type == EffectType::Cabinet) return;
    if (ampAt < 0 || speaker < 0.05f || (int)effects.size() >= MAX_EFFECTS) return;
    Effect cabinet = makeEffect(EffectType::Cabinet);
    cabinet.values[0] = 0.0f; // the open 1x12: the old speaker's gentle top
    cabinet.values[1] = 0.6f - 0.5f * speaker;
    effects.insert(effects.begin() + ampAt + 1, cabinet);
}

bool readTone(const std::string& text, Tone& tone, std::string& error){
    std::istringstream lines(text);
    std::string line;
    bool started = false;
    int version = 1;
    int oldAmpAt = -1;       // an older tone's first amp, and how much of its speaker it used (the standard, if unsaid)
    float oldSpeaker = 0.6f;
    Tone read;
    while (std::getline(lines, line)){
        if (!line.empty() && line.back() == '\r') line.pop_back();
        std::istringstream words(line);
        std::string key;
        if (!(words >> key) || key[0] == '#') continue;
        if (!started){
            if (key != "hardthz_tone" && key != "lahn_tone"){ // (lahn: the game's name before)
                error = "not a hardthz tone";
                return false;
            }
            if (!(words >> version)) version = 1;
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
        std::vector<std::pair<std::string, float>> settings; // as written: an older tone's are made new below
        while (words >> id){
            if (id == "file"){
                std::string name;
                std::getline(words >> std::ws, name);
                setEffectFile(effect, name);
                break;
            }
            if (!(words >> value)) break;
            settings.push_back({ id, value });
        }
        if (version < 2) upgradeEffect(effect, settings, read.effects);
        for (const auto& [setting, written] : settings){
            for (int i = 0; i < info.parameterCount; i++){
                if (setting == info.parameters[i].id) effect.values[i] = std::clamp(written, info.parameters[i].min, info.parameters[i].max);
            }
        }
        if (version < 2 && effect.type == EffectType::Amp && oldAmpAt < 0){
            oldAmpAt = (int)read.effects.size();
            for (const auto& [setting, written] : settings) if (setting == "cabinet") oldSpeaker = written;
        }
        read.effects.push_back(effect);
    }
    if (!started){
        error = "not a hardthz tone";
        return false;
    }
    if (version < 2) upgradeAmpSpeaker(oldSpeaker, oldAmpAt, read.effects);
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

// Works out an effect's filters for its settings (keeping the filters' memory, so nothing clicks)
static void setUp(EffectState& state, const Effect& effect, float rate){
    const float* v = effect.values;
    switch (effect.type){
        case EffectType::Drive: setUpDrive(state.drive, v, rate); break;
        case EffectType::Amp: setUpAmp(state.amp, v, rate); break;
        case EffectType::Equalizer:
            setHighPass(state.filters[0], v[0], 0.7f, rate);
            setShelf(state.filters[1], 100.0f, v[1], false, rate);
            setPeak(state.filters[2], 400.0f, 1.0f, v[2], rate);
            setPeak(state.filters[3], 1500.0f, 1.0f, v[3], rate);
            setShelf(state.filters[4], 5000.0f, v[4], true, rate);
            break;
        case EffectType::Octaver:
            setLowPass(state.filters[0], 250.0f, 0.7f, rate); // the fundamental alone, for the zero crossings
            setLowPass(state.filters[1], 250.0f, 0.7f, rate);
            setLowPass(state.filters[2], 150.0f * std::pow(10.0f, v[2]), 0.7f, rate); // the square wave, rounded off
            break;
        case EffectType::Delay:
            setLowPass(state.filters[0], 1000.0f * std::pow(12.0f, v[3]), 0.7f, rate); // each echo a little darker
            break;
        case EffectType::Cabinet:
            setShelf(state.filters[0], 2500.0f, 3.0f - 11.0f * v[1], true, rate); // the mic: at the middle bright, toward the edge dark...
            setPeak(state.filters[1], 350.0f, 0.8f, 2.5f * v[1], rate);           // ...and fuller
            setHighPass(state.filters[2], v[2], 0.7f, rate);
            setLowPass(state.filters[3], v[3], 0.7f, rate);
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
            state.amp = AmpState{};
            state.drive = DriveState{};
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
        state.amp = AmpState{};
        state.drive = DriveState{};
    }
    for (int i = 0; i < chain.parameters.count; i++) setUp(chain.states[i], chain.parameters.effects[i], (float)chain.sampleRate); // their settings again
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
#ifdef HARDTHZ_SSE
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
        case EffectType::Drive: return processDrive(state.drive, x);
        case EffectType::Amp: return processAmp(state.amp, x);
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

// A capture: its model plays the block, after the input gain, brought to a common loudness, then its level. Without
// one (no file yet, or it couldn't be read), the sound goes through as it is.
static void processCapture(const Effect& effect, const ToneAsset* asset, int runner, float* samples, int count, int sampleRate){
    CaptureModel* model = asset && runner >= 0 && runner < TONE_RUNNERS ? asset->models[runner].get() : nullptr;
    if (!model) return;
    const float input = dbToGain(effect.values[0]), output = dbToGain(-18.0f - asset->loudnessDb + effect.values[1]);
    for (int i = 0; i < count; i++) samples[i] *= input;
    model->process(samples, count, sampleRate);
    for (int i = 0; i < count; i++) samples[i] = std::isfinite(samples[i]) ? samples[i] * output : 0.0f;
}

// Effect by effect, each over the whole block: the same as sample by sample through them all, since none looks ahead,
// and a capture's model runs best a block at a time
void processToneChain(ToneChain& chain, float* samples, int count){
    const ToneParameters& parameters = chain.parameters;
    const float rate = (float)chain.sampleRate;
    for (int i = 0; i < count; i++){
        float x = samples[i];
        float blocked = x - chain.dcIn + DC_POLE * chain.dcOut; // no DC: it would only eat into the headroom
        chain.dcIn = x;
        chain.dcOut = quiet(blocked);
        samples[i] = blocked;
    }
    for (int e = 0; e < parameters.count; e++){
        const Effect& effect = parameters.effects[e];
        if (!effect.on) continue;
        if (effect.type == EffectType::Capture){
            processCapture(effect, parameters.assets[e], chain.runner, samples, count, chain.sampleRate);
            continue;
        }
        EffectState& state = chain.states[e];
        for (int i = 0; i < count; i++) samples[i] = process(state, effect, samples[i], rate);
    }
    for (int i = 0; i < count; i++){
        const float y = samples[i] * parameters.volume;
        samples[i] = std::clamp(std::isfinite(y) ? y : 0.0f, -1.0f, 1.0f); // never past full scale, whatever the tone
    }
}
