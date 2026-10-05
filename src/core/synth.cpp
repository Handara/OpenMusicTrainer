#include "core/synth.h"

#include "core/settings.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <random>
#include <vector>

const float PLUCK_DECAY_S = 1.5f; // time for a note to fade by 60 dB
const float END_FADE_S = 0.01f;   // short fade at the very end so a sound never stops with a click
const float PEAK_LEVEL = 0.5f;    // every sound peaks around half of full scale, so they can overlap safely
const double TWO_PI = 6.283185307179586;

// Multiplier per sample that makes a sound fade by 60 dB (to 1/1000) over `seconds`
static float decayPerSample(float seconds, int sampleRate){
    return std::pow(0.001f, 1.0f / (seconds * sampleRate));
}

// Linear fade over the last few milliseconds, reaching exactly 0 on the last sample
static void fadeEnd(float* out, int count, int sampleRate){
    int fadeLength = std::min(count - 1, (int)(END_FADE_S * sampleRate));
    for (int i = 0; i < fadeLength; i++) out[count - 1 - i] *= (float)i / fadeLength;
}

void renderPluck(float* out, int count, float frequency, int sampleRate, unsigned seed){
    // The loop must delay the signal by exactly one period. Three parts add up to it:
    // the delay line (whole samples) + the averaging filter (always half a sample) + an allpass filter
    // (the leftover fraction). Without the allpass, high notes would be up to ~20 cents out of tune.
    float period = sampleRate / frequency;
    int lineLength = std::max(2, (int)(period - 0.5f - 0.1f)); // keeps the fraction in 0.1..1.1, where the allpass is accurate
    float fraction = period - 0.5f - lineLength;
    float allpassCoefficient = (1.0f - fraction) / (1.0f + fraction);

    // Loss per trip around the loop, so every pitch takes the same time to fade (high notes loop more often)
    float loopGain = std::pow(10.0f, -3.0f / (PLUCK_DECAY_S * frequency));

    // The pluck itself: noise, slightly low-passed so it sounds like a finger rather than a pick scratch
    std::vector<float> line(lineLength);
    std::minstd_rand rng(seed);
    std::uniform_real_distribution<float> noise(-1.0f, 1.0f);
    float smoothed = 0.0f;
    for (float& sample : line){
        smoothed += 0.5f * (noise(rng) - smoothed);
        sample = smoothed;
    }

    float previous = 0.0f, allpassIn = 0.0f, allpassOut = 0.0f;
    for (int i = 0, position = 0; i < count; i++){
        float current = line[position];
        float averaged = 0.5f * (current + previous) * loopGain;
        previous = current;
        float delayed = allpassCoefficient * averaged + allpassIn - allpassCoefficient * allpassOut;
        allpassIn = averaged;
        allpassOut = delayed;
        line[position] = delayed;
        position = (position + 1) % lineLength;

        out[i] = PEAK_LEVEL * current;
    }
    fadeEnd(out, count, sampleRate);
}

void renderSoftTone(float* out, int count, float frequency, int sampleRate){
    const float attackSamples = 0.008f * sampleRate; // 8 ms: soft, but no audible delay
    const float decay = decayPerSample(1.2f, sampleRate);
    float envelope = 1.0f;
    for (int i = 0; i < count; i++){
        double phase = TWO_PI * frequency * i / sampleRate;
        float tone = (float)(std::sin(phase) + 0.3 * std::sin(2 * phase) + 0.1 * std::sin(3 * phase)) / 1.4f;
        float attack = std::min(1.0f, i / attackSamples);
        out[i] = PEAK_LEVEL * tone * attack * envelope;
        envelope *= decay;
    }
    fadeEnd(out, count, sampleRate);
}

void renderKeys(float* out, int count, float frequency, int sampleRate){
    const float attackSamples = 0.003f * sampleRate;
    const float decay = decayPerSample(1.5f, sampleRate);
    const float brightnessDecay = decayPerSample(0.6f, sampleRate); // the wobble fades faster than the note
    float envelope = 1.0f, modulationIndex = 1.5f;
    for (int i = 0; i < count; i++){
        double phase = TWO_PI * frequency * i / sampleRate;
        float tone = (float)std::sin(phase + modulationIndex * std::sin(phase)); // modulator at the same frequency: harmonic, in tune
        float attack = std::min(1.0f, i / attackSamples);
        out[i] = PEAK_LEVEL * tone * attack * envelope;
        envelope *= decay;
        modulationIndex *= brightnessDecay;
    }
    fadeEnd(out, count, sampleRate);
}

void renderBass(float* out, int count, float frequency, int sampleRate){
    // Each harmonic decays at its own rate: the higher, the sooner, so the attack is bright and the note settles round
    const float fundamentalDecay = decayPerSample(2.2f, sampleRate);
    const float secondDecay = decayPerSample(0.6f, sampleRate);
    const float thirdDecay = decayPerSample(0.25f, sampleRate);
    const float thumpDecay = decayPerSample(0.015f, sampleRate);
    const float attackSamples = 0.004f * sampleRate; // 4 ms: no click, still immediate
    float fundamental = 1.0f, second = 0.45f, third = 0.25f, thump = 0.3f;
    for (int i = 0; i < count; i++){
        double w = TWO_PI * frequency * i / sampleRate;
        float attack = std::min(1.0f, i / attackSamples);
        float v = fundamental * (float)std::sin(w) + second * (float)std::sin(2.0 * w) + third * (float)std::sin(3.0 * w)
                + thump * (float)std::sin(0.5 * w); // a low thump under the note, gone in a few cycles
        out[i] = PEAK_LEVEL * 0.6f * v * attack;
        fundamental *= fundamentalDecay;
        second *= secondDecay;
        third *= thirdDecay;
        thump *= thumpDecay;
    }
    fadeEnd(out, count, sampleRate);
}

// --- String instruments -------------------------------------------------------------------------------

// What makes one string instrument sound unlike another
struct StringShape {
    float pluckAt;      // where the string is plucked, as a part of its length from the bridge: nearer, brighter
    float pickupAt;     // where the pickup listens
    float toneHz;       // above this the pickup, the tone knob and the amp let less and less through
    float topHz;        // no harmonic is made above this
    int maxHarmonics;
    float body;         // the fundamental, boosted: an amp's low end
    float sustainS;     // how long the fundamental of a note at 110 Hz rings (to -60 dB)
    float sustainSlope; // higher notes ring shorter: sustain goes as (110 / frequency) ^ slope
    float damping;      // how much sooner each higher harmonic dies
    float snap;         // how much brighter the attack is than the note it settles into
    float snapS;        // and for how long
    float stiffness;    // a stiff string's harmonics run a little sharp
    float attackS;      // the note's rise: no click
    float noise;        // the finger or the pick on the string, against the note
    float noiseHz;      // dull (a finger's thump) or bright (a pick's tick)
    float noiseS;
    float chorus;       // how much of a slowly wandering copy is mixed in
    float level;        // the loudness every note is brought to (RMS over its first moments)
};

// Fingers over the pickup of a four-string: the second and third harmonics as strong as the fundamental (they carry
// a low E on small speakers), a growl that lasts a second, a slap's brightness for the first few hundredths
const StringShape BASS_SHAPE = { 0.21f, 0.17f, 1500.0f, 4500.0f, 28, 1.7f, 3.2f, 0.3f, 0.30f, 1.8f, 0.035f, 1.0e-4f, 0.003f,
                                 0.22f, 450.0f, 0.012f, 0.0f, 0.17f };
// A pick near the bridge heard at the neck pickup: glassy, long-ringing, a touch of chorus
const StringShape GUITAR_SHAPE = { 0.14f, 0.23f, 3800.0f, 8000.0f, 32, 1.0f, 3.6f, 0.3f, 0.09f, 0.8f, 0.02f, 4.0e-5f, 0.0015f,
                                   0.10f, 3200.0f, 0.004f, 0.28f, 0.12f };

const float STRING_MUTE_S = 0.05f;      // a note is muted this quickly at its end
const float STRING_LEVEL_OVER_S = 0.3f; // its loudness is measured over this much of its start
const float STRING_PEAK = 0.85f;        // and its peaks kept under this

void renderStringNote(float* out, int count, float frequency, int sampleRate, StringVoice voice, unsigned seed){
    const StringShape& shape = voice == StringVoice::Bass ? BASS_SHAPE : GUITAR_SHAPE;
    std::fill(out, out + count, 0.0f);
    if (count < 2) return;

    // The attack's extra brightness, fading: shared by every harmonic
    std::vector<float> snap(count);
    const float snapDecay = std::exp(-1.0f / (shape.snapS * sampleRate));
    float bright = 1.0f;
    for (float& value : snap){
        value = bright;
        bright *= snapDecay;
    }

    // Each harmonic: a sine turning at its own speed and fading at its own rate. Its size is what a string plucked
    // at one point and heard at another gives (both are standing-wave shapes: sin(k pi x)), over k; the high ones
    // die sooner, so each is only worked out for as long as it can be heard.
    const double pi = TWO_PI / 2;
    const float sustain = shape.sustainS * std::pow(110.0f / frequency, shape.sustainSlope);
    float total = 0.0f;
    for (int k = 1; k <= shape.maxHarmonics; k++){
        const double harmonic = k * (double)frequency * std::sqrt(1.0 + shape.stiffness * k * k);
        if (harmonic > shape.topHz || harmonic > sampleRate * 0.45) break;
        double size = std::sin(k * pi * shape.pluckAt) * std::sin(k * pi * shape.pickupAt);
        size += size < 0.0 ? -0.04 : 0.04; // never quite nothing: a real string is plucked over a width, not at a point
        size /= k;
        const double over = harmonic / shape.toneHz;
        size /= std::sqrt(1.0 + over * over * over * over);
        if (k == 1) size *= shape.body;
        total += (float)std::fabs(size);

        const float rings = sustain / (1.0f + shape.damping * std::pow((float)(k - 1), 1.4f));
        const double fade = std::pow(0.001, 1.0 / ((double)rings * sampleRate));
        const int heard = std::min(count, (int)(rings * 1.2f * sampleRate));
        const float snapped = shape.snap * std::min(1.0f, (k - 1) / 4.0f);
        const double turn = TWO_PI * harmonic / sampleRate, cosine = std::cos(turn), sine = std::sin(turn);
        double x = size, y = 0.0; // y is the sine, starting from 0
        for (int i = 0; i < heard; i++){
            out[i] += (float)y * (1.0f + snapped * snap[i]);
            const double nextX = (x * cosine - y * sine) * fade;
            y = (x * sine + y * cosine) * fade;
            x = nextX;
        }
    }

    // The note rises quickly rather than at once, and the finger's thump or the pick's tick goes with it
    const int attack = std::max(1, (int)(shape.attackS * sampleRate));
    for (int i = 0; i < attack && i < count; i++) out[i] *= (float)i / attack;
    std::minstd_rand rng(seed);
    std::uniform_real_distribution<float> noise(-1.0f, 1.0f);
    const float color = 1.0f - std::exp(-(float)TWO_PI * shape.noiseHz / sampleRate);
    const float noiseDecay = std::exp(-1.0f / (shape.noiseS * sampleRate));
    float smoothed = 0.0f, burst = shape.noise * total;
    for (int i = 0; i < count && burst > 1.0e-5f; i++){
        smoothed += color * (noise(rng) - smoothed);
        out[i] += smoothed * burst;
        burst *= noiseDecay;
    }

    // Chorus: the note again a few milliseconds later, the delay slowly wandering
    if (shape.chorus > 0.0f){
        std::vector<float> dry(out, out + count);
        for (int i = 0; i < count; i++){
            double at = i - (0.011 + 0.002 * std::sin(TWO_PI * 0.8 * i / sampleRate)) * sampleRate;
            if (at < 0.0) continue;
            int before = (int)at;
            float part = (float)(at - before);
            float delayed = dry[before] * (1.0f - part) + (before + 1 < count ? dry[before + 1] : 0.0f) * part;
            out[i] += shape.chorus * delayed;
        }
    }

    // Every note as loud as the next, whatever its pitch and however many harmonics it has
    const int measured = std::min(count, (int)(STRING_LEVEL_OVER_S * sampleRate));
    double energy = 0.0;
    float peak = 0.0f;
    for (int i = 0; i < measured; i++) energy += (double)out[i] * out[i];
    for (int i = 0; i < count; i++) peak = std::max(peak, std::fabs(out[i]));
    if (energy > 0.0 && peak > 0.0f){
        float gain = std::min(shape.level / (float)std::sqrt(energy / measured), STRING_PEAK / peak);
        for (int i = 0; i < count; i++) out[i] *= gain;
    }

    // Muted at its end
    const int mute = std::min(count - 1, (int)(STRING_MUTE_S * sampleRate));
    for (int i = 0; i < mute; i++){
        float left = (float)i / mute;
        out[count - 1 - i] *= left * left * (3.0f - 2.0f * left);
    }
    out[count - 1] = 0.0f;
}

void renderDrop(float* out, int count, float frequency, int sampleRate){
    const float glideSamples = 0.025f * sampleRate; // reaches the note's pitch after 25 ms
    const float decay = decayPerSample(0.35f, sampleRate);
    const float attackSamples = 0.001f * sampleRate;
    float envelope = 1.0f;
    double phase = 0.0;
    for (int i = 0; i < count; i++){
        // Frequency slides up from 60% of the note (a bit under an octave below) to the note, easing in
        float progress = std::min(1.0f, i / glideSamples);
        float glide = 0.6f + 0.4f * (1.0f - (1.0f - progress) * (1.0f - progress));
        phase += TWO_PI * frequency * glide / sampleRate; // accumulate: the frequency changes as it goes
        float attack = std::min(1.0f, i / attackSamples);
        out[i] = PEAK_LEVEL * (float)std::sin(phase) * attack * envelope;
        envelope *= decay;
    }
    fadeEnd(out, count, sampleRate);
}

void renderClick(float* out, int count, int sampleRate, bool accent){
    const float frequency = accent ? 1760.0f : 1320.0f; // A6 or E6: bright enough to cut through music
    const float toneDecay = decayPerSample(0.03f, sampleRate);
    const float noiseDecay = decayPerSample(0.004f, sampleRate);
    std::minstd_rand rng(7);
    std::uniform_real_distribution<float> noise(-1.0f, 1.0f);
    float toneEnvelope = 1.0f, noiseEnvelope = 1.0f;
    for (int i = 0; i < count; i++){
        float tone = (float)std::sin(TWO_PI * frequency * i / sampleRate);
        out[i] = PEAK_LEVEL * (0.7f * tone * toneEnvelope + 0.3f * noise(rng) * noiseEnvelope);
        toneEnvelope *= toneDecay;
        noiseEnvelope *= noiseDecay;
    }
    fadeEnd(out, count, sampleRate);
}

void renderDrum(float* out, int count, int sampleRate, bool high){
    std::minstd_rand rng(high ? 11 : 13);
    std::uniform_real_distribution<float> noise(-1.0f, 1.0f);
    if (high){
        const float toneDecay = decayPerSample(0.025f, sampleRate), noiseDecay = decayPerSample(0.008f, sampleRate);
        float toneEnvelope = 1.0f, noiseEnvelope = 1.0f;
        for (int i = 0; i < count; i++){
            float tone = (float)std::sin(TWO_PI * 2100.0 * i / sampleRate);
            out[i] = PEAK_LEVEL * (0.45f * tone * toneEnvelope + 0.55f * noise(rng) * noiseEnvelope);
            toneEnvelope *= toneDecay;
            noiseEnvelope *= noiseDecay;
        }
    } else {
        const float bodyDecay = decayPerSample(0.45f, sampleRate), thumpDecay = decayPerSample(0.012f, sampleRate);
        float bodyEnvelope = 1.0f, thumpEnvelope = 1.0f;
        double phase = 0.0;
        for (int i = 0; i < count; i++){
            // The skin's pitch drops fast after the hit, then settles
            float t = (float)i / sampleRate;
            float frequency = 60.0f + 110.0f * std::exp(-t / 0.035f);
            phase += TWO_PI * frequency / sampleRate;
            out[i] = PEAK_LEVEL * (0.85f * (float)std::sin(phase) * bodyEnvelope + 0.25f * noise(rng) * thumpEnvelope);
            bodyEnvelope *= bodyDecay;
            thumpEnvelope *= thumpDecay;
        }
    }
    fadeEnd(out, count, sampleRate);
}

bool renderBuiltInSound(const char* name, float* out, int count, float frequency, int sampleRate, unsigned seed){
    if (std::strcmp(name, "pluck") == 0) renderPluck(out, count, frequency, sampleRate, seed);
    else if (std::strcmp(name, "soft") == 0) renderSoftTone(out, count, frequency, sampleRate);
    else if (std::strcmp(name, "keys") == 0) renderKeys(out, count, frequency, sampleRate);
    else if (std::strcmp(name, "drop") == 0) renderDrop(out, count, frequency, sampleRate);
    else return false;
    return true;
}

// A two-pole band-pass (the RBJ cookbook's, its peak at 0 dB): a vowel's formant, a drum's ring
struct SynthBandPass {
    float b0 = 0.0f, a1 = 0.0f, a2 = 0.0f, x1 = 0.0f, x2 = 0.0f, y1 = 0.0f, y2 = 0.0f;
    void set(float frequency, float q, int sampleRate){
        const double w = TWO_PI * std::min(frequency, 0.45f * sampleRate) / sampleRate, alpha = std::sin(w) / (2.0 * q), a0 = 1.0 + alpha;
        b0 = (float)(alpha / a0);
        a1 = (float)(-2.0 * std::cos(w) / a0);
        a2 = (float)((1.0 - alpha) / a0);
    }
    float run(float x){
        const float y = b0 * (x - x2) - a1 * y1 - a2 * y2;
        x2 = x1;
        x1 = x;
        y2 = y1;
        y1 = y;
        return y;
    }
};

// A one-pole high-pass: what's above `frequency` through, what's below it fading
struct SynthHighPass {
    float alpha = 0.0f, x1 = 0.0f, y1 = 0.0f;
    void set(float frequency, int sampleRate){
        const float rc = 1.0f / (float)(TWO_PI * frequency), dt = 1.0f / sampleRate;
        alpha = rc / (rc + dt);
    }
    float run(float x){
        y1 = alpha * (y1 + x - x1);
        x1 = x;
        return y1;
    }
};

// A hi-hat's metal: six square waves at the clashing pitches the TR-808 used
static float metal(double t){
    static const double PITCHES[6] = { 205.3, 304.4, 369.6, 522.7, 540.0, 800.0 };
    float sum = 0.0f;
    for (double pitch : PITCHES) sum += std::fmod(t * pitch * 2.0, 2.0) < 1.0 ? 1.0f : -1.0f;
    return sum / 6.0f;
}

void renderKitDrum(float* out, int count, int sampleRate, KitDrum drum, unsigned seed){
    std::minstd_rand rng(seed * 7919u + (unsigned)drum + 1u);
    std::uniform_real_distribution<float> noise(-1.0f, 1.0f);
    switch (drum){
        case KitDrum::Kick: {
            double phase = 0.0;
            const float drive = 1.8f;
            for (int i = 0; i < count; i++){
                const float t = (float)i / sampleRate;
                phase += TWO_PI * (48.0f + 110.0f * std::exp(-t / 0.028f)) / sampleRate;
                const float body = (float)std::sin(phase) * std::exp(-t / 0.16f), click = noise(rng) * std::exp(-t / 0.002f) * 0.3f;
                out[i] = PEAK_LEVEL * std::tanh(drive * (body + click)) / std::tanh(drive);
            }
            break;
        }
        case KitDrum::Snare: {
            SynthHighPass high;
            high.set(1500.0f, sampleRate);
            for (int i = 0; i < count; i++){
                const float t = (float)i / sampleRate;
                const float tone = 0.5f * (float)std::sin(TWO_PI * 185.0 * t) * std::exp(-t / 0.05f)
                                 + 0.3f * (float)std::sin(TWO_PI * 330.0 * t) * std::exp(-t / 0.03f);
                out[i] = PEAK_LEVEL * (0.6f * tone + 0.8f * high.run(noise(rng)) * std::exp(-t / 0.075f));
            }
            break;
        }
        case KitDrum::Hat: case KitDrum::OpenHat: case KitDrum::Crash: {
            const float decay = drum == KitDrum::Hat ? 0.018f : drum == KitDrum::OpenHat ? 0.13f : 0.55f;
            const float level = drum == KitDrum::Crash ? 1.1f : 1.0f;
            SynthHighPass high1, high2;
            high1.set(drum == KitDrum::Crash ? 3500.0f : 6500.0f, sampleRate);
            high2.set(drum == KitDrum::Crash ? 3500.0f : 6500.0f, sampleRate);
            for (int i = 0; i < count; i++){
                const float t = (float)i / sampleRate;
                const float attack = drum == KitDrum::Crash ? std::min(1.0f, t / 0.003f) : 1.0f;
                const float sound = high2.run(high1.run(0.6f * noise(rng) + 0.5f * metal(t)));
                out[i] = PEAK_LEVEL * level * 2.0f * sound * attack * std::exp(-t / decay);
            }
            break;
        }
    }
    fadeEnd(out, count, sampleRate);
}

// A sawtooth without the harsh aliasing of a naive one: its jump rounded off over a sample either side (PolyBLEP)
static float softSaw(double phase, double step){
    float saw = (float)(2.0 * phase - 1.0);
    if (phase < step){
        const double t = phase / step;
        saw -= (float)(t + t - t * t - 1.0);
    } else if (phase > 1.0 - step){
        const double t = (phase - 1.0) / step;
        saw -= (float)(t * t + t + t + 1.0);
    }
    return saw;
}

// A vowel: its first three formants (Hz)
struct CrowdVowel { float f1, f2, f3; };
static CrowdVowel mixVowels(const CrowdVowel& a, const CrowdVowel& b, float along){
    along = std::clamp(along, 0.0f, 1.0f);
    return { a.f1 + (b.f1 - a.f1) * along, a.f2 + (b.f2 - a.f2) * along, a.f3 + (b.f3 - a.f3) * along };
}

void renderCrowd(float* out, int count, int sampleRate, CrowdReaction reaction, unsigned seed){
    std::fill(out, out + count, 0.0f);
    const bool cheer = reaction == CrowdReaction::Cheer, claps = reaction == CrowdReaction::Claps;
    std::minstd_rand rng(seed * 104729u + (unsigned)reaction + 1u);
    auto uniform = [&](float low, float high){ return std::uniform_real_distribution<float>(low, high)(rng); };
    std::uniform_real_distribution<float> noise(-1.0f, 1.0f);
    // "yay": from the y's vowel to an open "eh"; "aw": an open "a" darkening to "aw"
    const CrowdVowel Y = { 300, 2200, 2900 }, EH = { 700, 1750, 2600 }, A = { 750, 1150, 2500 }, AW = { 580, 880, 2450 };
    const int voices = cheer ? 20 : claps ? 0 : 15;
    const float length = (float)count / sampleRate;
    for (int v = 0; v < voices; v++){
        const float start = uniform(0.0f, cheer ? 0.18f : 0.12f);
        const float span = std::min(length - start - 0.05f, uniform(cheer ? 0.9f : 1.0f, cheer ? 1.5f : 1.5f));
        const float base = uniform(150.0f, 400.0f);                  // low voices to children's
        const float size = 1.0f + (base - 150.0f) / 250.0f * 0.18f; // smaller voices, higher formants
        const float vibratoRate = uniform(4.5f, 7.0f), vibratoDepth = uniform(0.012f, 0.035f), loud = uniform(0.5f, 1.0f);
        const float vibratoPhase = uniform(0.0f, 6.28f);
        SynthBandPass formant[3];
        double phase = uniform(0.0f, 1.0f);
        const int first = (int)(start * sampleRate), last = std::min(count, first + (int)(span * sampleRate));
        for (int i = first; i < last; i++){
            const float t = (float)(i - first) / sampleRate, along = t / span;
            // The pitch: a cheer jumps up and holds, falling a little at the end; an aww falls all the way
            float pitch = cheer ? base * (1.0f + 0.25f * std::min(1.0f, t / 0.12f) - 0.15f * std::max(0.0f, along - 0.7f) / 0.3f)
                                : base * (1.1f - 0.38f * along * (2.0f - along));
            pitch *= 1.0f + vibratoDepth * std::sin(vibratoRate * 6.2832f * t + vibratoPhase) * std::min(1.0f, t / 0.25f);
            const double step = pitch / sampleRate;
            phase += step;
            if (phase >= 1.0) phase -= 1.0;
            if ((i - first) % 64 == 0){
                const CrowdVowel vowel = cheer ? mixVowels(Y, EH, t / 0.12f) : mixVowels(A, AW, along * 1.5f);
                formant[0].set(vowel.f1 * size, 6.0f, sampleRate);
                formant[1].set(vowel.f2 * size, 12.0f, sampleRate);
                formant[2].set(vowel.f3 * size, 16.0f, sampleRate);
            }
            const float source = softSaw(phase, step) + 0.08f * noise(rng); // the buzz, and a little breath
            const float voiced = formant[0].run(source) + 0.6f * formant[1].run(source) + 0.3f * formant[2].run(source);
            const float envelope = std::min(1.0f, t / (cheer ? 0.03f : 0.08f)) * (along > 0.75f ? std::cos((along - 0.75f) / 0.25f * 1.5708f) : 1.0f);
            out[i] += voiced * envelope * loud;
        }
    }
    if (cheer || claps){
        // Clapping, thick at first and thinning out (a cheer's), or a few hands, unhurried (polite)
        const int hands = cheer ? 50 : 16;
        for (int c = 0; c < hands; c++){
            const float at = cheer ? 0.05f + 1.9f * std::pow(uniform(0.0f, 1.0f), 1.6f) : 0.03f + 1.3f * std::pow(uniform(0.0f, 1.0f), 1.3f);
            SynthBandPass ring;
            ring.set(uniform(900.0f, 2200.0f), 1.5f, sampleRate);
            const float loud = uniform(0.6f, 1.6f);
            const int first = (int)(at * sampleRate), last = std::min(count, first + (int)(0.06f * sampleRate));
            for (int i = first; i < last; i++) out[i] += loud * ring.run(noise(rng)) * std::exp(-(float)(i - first) / sampleRate / 0.012f);
        }
    }
    if (cheer){
        // Someone whistling
        double phase = 0.0;
        const float whistleAt = uniform(0.15f, 0.35f);
        const int first = (int)(whistleAt * sampleRate), last = std::min(count, first + (int)(0.8f * sampleRate));
        for (int i = first; i < last; i++){
            const float t = (float)(i - first) / sampleRate;
            const float pitch = t < 0.22f ? 1800.0f + 900.0f * t / 0.22f : 2700.0f - 700.0f * std::max(0.0f, t - 0.5f) / 0.3f;
            phase += TWO_PI * pitch / sampleRate;
            const float envelope = std::min(1.0f, t / 0.04f) * (t > 0.6f ? 1.0f - (t - 0.6f) / 0.2f : 1.0f);
            out[i] += 0.5f * (float)std::sin(phase) * envelope;
        }
    }
    // A small room: two echoes going round, a little of them under the crowd
    const int echo1 = (int)(0.037f * sampleRate), echo2 = (int)(0.053f * sampleRate);
    std::vector<float> room1(count, 0.0f), room2(count, 0.0f);
    for (int i = 0; i < count; i++){
        room1[i] = out[i] + (i >= echo1 ? 0.4f * room1[i - echo1] : 0.0f);
        room2[i] = out[i] + (i >= echo2 ? 0.35f * room2[i - echo2] : 0.0f);
    }
    float peak = 0.0f;
    for (int i = 0; i < count; i++){
        out[i] = out[i] + 0.25f * (room1[i] + room2[i] - 2.0f * out[i]);
        peak = std::max(peak, std::abs(out[i]));
    }
    const float level = claps ? 0.6f : 0.9f; // polite claps, dimmer
    if (peak > 0.0f) for (int i = 0; i < count; i++) out[i] *= PEAK_LEVEL * level / peak;
    fadeEnd(out, count, sampleRate);
}
