#include "core/ampmodel.h"

#include <algorithm>
#include <cmath>
#include <complex>

namespace {

const double PI_D = 3.14159265358979323846;
const float BUTTERWORTH_Q[4] = { 0.5098f, 0.6013f, 0.9000f, 2.5629f }; // an eighth-order Butterworth, as four biquads
const float INPUT_TRIM = 2.0f; // a pickup's level, about where an amp's first tube expects it

// An amp: its tone stack's parts and how its stages, power amp and controls behave
struct AmpDesign {
    const char* name;
    double R1, R2, R3, R4, C1, C2, C3; // the tone stack's (treble, bass and middle pots, the slope resistor; capacitors)
    int stages;
    float tightHz;           // a high-pass before the stages: high-gain amps keep the lows out of the distortion
    float brightDb;          // the bright capacitor's lift of the highs into the first stage, fading as the gain goes up
    float gainLow, gainHigh; // each stage's gain with the Gain knob at its bottom and at its top
    float bias;              // how lopsided the tubes clip: even harmonics, a tube's warmth
    float couplingHz;        // each stage's coupling capacitor: lows lost between stages (tighter when higher)
    float millerHz;          // each tube's own capacitance: highs lost
    float powerDrive;        // how hard the power amp is pushed
    float sag;               // how much its supply droops on hard playing
    float presenceHz;        // where the presence knob works
    float depthHz, depthDb;  // the power amp's low resonance with its speaker
    float cleanBlend;        // the clean lows kept under it (a modern bass preamp's), 0 for none
    float level;             // so every model comes out about as loud
};

// The tone stack parts are the amps' own, as Guitarix lists them (after D.T. Yeh's analysis)
const AmpDesign AMPS[] = {
    // A '69 Twin Reverb: loud and clean, scooped mids, sparkle
    { "Clean US", 250e3, 250e3, 10e3, 100e3, 120e-12, 100e-9, 47e-9,
      2, 20.0f, 4.0f, 0.6f, 6.0f, 0.2f, 20.0f, 9000.0f, 0.7f, 0.15f, 4000.0f, 90.0f, 1.5f, 0.0f, 1.2f },
    // A JTM45 / Plexi: the classic rock crunch, its mids forward
    { "Crunch UK", 250e3, 1e6, 25e3, 33e3, 270e-12, 22e-9, 22e-9,
      3, 40.0f, 3.0f, 1.0f, 9.0f, 0.3f, 35.0f, 7000.0f, 1.5f, 0.35f, 3500.0f, 100.0f, 2.0f, 0.0f, 1.3f },
    // A JCM800 2203: more gain, brighter and tighter
    { "Lead UK", 220e3, 1e6, 22e3, 33e3, 470e-12, 22e-9, 22e-9,
      3, 80.0f, 4.0f, 1.8f, 18.0f, 0.35f, 60.0f, 6500.0f, 1.4f, 0.25f, 3500.0f, 100.0f, 2.0f, 0.0f, 1.1f },
    // A Soldano SLO: four stages, the lows kept tight before them, for modern rhythm and lead
    { "Modern", 250e3, 1e6, 25e3, 47e3, 470e-12, 20e-9, 20e-9,
      4, 140.0f, 2.0f, 2.2f, 22.0f, 0.4f, 90.0f, 6000.0f, 1.2f, 0.15f, 4000.0f, 85.0f, 3.0f, 0.0f, 0.85f },
    // A '59 Bassman 5F6-A: the bass amp guitarists borrowed, warm and round, breaking up softly
    { "Bass Tube", 250e3, 1e6, 25e3, 56e3, 250e-12, 20e-9, 20e-9,
      2, 15.0f, 2.0f, 0.8f, 7.0f, 0.25f, 12.0f, 8000.0f, 1.2f, 0.4f, 2500.0f, 60.0f, 2.0f, 0.0f, 1.4f },
    // A modern bass preamp: only the highs driven, the lows kept clean beneath them
    { "Bass Drive", 250e3, 250e3, 25e3, 56e3, 250e-12, 47e-9, 47e-9,
      3, 250.0f, 3.0f, 1.5f, 16.0f, 0.3f, 150.0f, 5000.0f, 1.0f, 0.1f, 3000.0f, 70.0f, 2.0f, 1.0f, 1.0f },
};
const int AMP_COUNT = (int)(sizeof AMPS / sizeof AMPS[0]);

const AmpDesign& ampDesign(int model){ return AMPS[std::clamp(model, 0, AMP_COUNT - 1)]; }

// tanh, nearly: a rational curve that meets ±1 at ±3 and stays there. A fraction of the cost, run four times a sample
// by every stage.
float softClip(float x){
    x = std::clamp(x, -3.0f, 3.0f);
    const float x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}

void setOnePole(OnePole& f, float frequency, float rate){
    f.coefficient = std::exp(-2.0f * (float)PI_D * std::min(frequency, rate * 0.45f) / rate);
}
float highPassOne(OnePole& f, float x){
    const float y = f.coefficient * (f.state + x - f.last);
    f.last = x;
    f.state = std::fabs(y) < 1e-15f ? 0.0f : y;
    return y;
}
float lowPassOne(OnePole& f, float x){
    f.state += (1.0f - f.coefficient) * (x - f.state);
    if (std::fabs(f.state) < 1e-15f) f.state = 0.0f;
    return f.state;
}

// One sample through `shape` four times as fast: zeros between (made up for by 4x), the images filtered off before,
// the harmonics above the device's rate filtered off after, one kept of four
template <typename Shape>
float oversampled(Oversampler& o, float x, Shape&& shape){
    float kept = 0.0f;
    for (int k = 0; k < OVERSAMPLING; k++){
        float u = k == 0 ? x * (float)OVERSAMPLING : 0.0f;
        for (Biquad& f : o.up) u = f.process(u);
        u = shape(u);
        for (Biquad& f : o.down) u = f.process(u);
        kept = u;
    }
    return kept;
}

// The tone stack's third-order response from its parts and pots (Yeh and Smith's), by the bilinear transform at
// `rate`: b and a, a[0] = 1. The bass pot is a log one.
void toneStackCoefficients(const AmpDesign& d, float treble, float mid, float bass, double rate, double b[4], double a[4]){
    const double t = std::clamp(treble, 0.0f, 1.0f), m = std::clamp(mid, 0.0f, 1.0f);
    const double l = std::exp((std::clamp(bass, 0.0f, 1.0f) - 1.0) * 3.4);
    const double R1 = d.R1, R2 = d.R2, R3 = d.R3, R4 = d.R4, C1 = d.C1, C2 = d.C2, C3 = d.C3;
    const double b1 = t * C1 * R1 + m * C3 * R3 + l * (C1 * R2 + C2 * R2) + (C1 * R3 + C2 * R3);
    const double b2 = t * (C1 * C2 * R1 * R4 + C1 * C3 * R1 * R4) - m * m * (C1 * C3 * R3 * R3 + C2 * C3 * R3 * R3)
                    + m * (C1 * C3 * R1 * R3 + C1 * C3 * R3 * R3 + C2 * C3 * R3 * R3)
                    + l * (C1 * C2 * R1 * R2 + C1 * C2 * R2 * R4 + C1 * C3 * R2 * R4)
                    + l * m * (C1 * C3 * R2 * R3 + C2 * C3 * R2 * R3)
                    + (C1 * C2 * R1 * R3 + C1 * C2 * R3 * R4 + C1 * C3 * R3 * R4);
    const double b3 = l * m * (C1 * C2 * C3 * R1 * R2 * R3 + C1 * C2 * C3 * R2 * R3 * R4)
                    - m * m * (C1 * C2 * C3 * R1 * R3 * R3 + C1 * C2 * C3 * R3 * R3 * R4)
                    + m * (C1 * C2 * C3 * R1 * R3 * R3 + C1 * C2 * C3 * R3 * R3 * R4)
                    + t * C1 * C2 * C3 * R1 * R3 * R4 - t * m * C1 * C2 * C3 * R1 * R3 * R4
                    + t * l * C1 * C2 * C3 * R1 * R2 * R4;
    const double a0 = 1.0;
    const double a1 = (C1 * R1 + C1 * R3 + C2 * R3 + C2 * R4 + C3 * R4) + m * C3 * R3 + l * (C1 * R2 + C2 * R2);
    const double a2 = m * (C1 * C3 * R1 * R3 - C2 * C3 * R3 * R4 + C1 * C3 * R3 * R3 + C2 * C3 * R3 * R3)
                    + l * m * (C1 * C3 * R2 * R3 + C2 * C3 * R2 * R3)
                    - m * m * (C1 * C3 * R3 * R3 + C2 * C3 * R3 * R3)
                    + l * (C1 * C2 * R2 * R4 + C1 * C2 * R1 * R2 + C1 * C3 * R2 * R4 + C2 * C3 * R2 * R4)
                    + (C1 * C2 * R1 * R4 + C1 * C3 * R1 * R4 + C1 * C2 * R3 * R4 + C1 * C2 * R1 * R3 + C1 * C3 * R3 * R4 + C2 * C3 * R3 * R4);
    const double a3 = l * m * (C1 * C2 * C3 * R1 * R2 * R3 + C1 * C2 * C3 * R2 * R3 * R4)
                    - m * m * (C1 * C2 * C3 * R1 * R3 * R3 + C1 * C2 * C3 * R3 * R3 * R4)
                    + m * (C1 * C2 * C3 * R3 * R3 * R4 + C1 * C2 * C3 * R1 * R3 * R3 - C1 * C2 * C3 * R1 * R3 * R4)
                    + l * C1 * C2 * C3 * R1 * R2 * R4
                    + C1 * C2 * C3 * R1 * R3 * R4;
    const double c = 2.0 * rate, c2 = c * c, c3 = c2 * c;
    const double B[4] = { -b1 * c - b2 * c2 - b3 * c3, -b1 * c + b2 * c2 + 3 * b3 * c3, b1 * c + b2 * c2 - 3 * b3 * c3, b1 * c - b2 * c2 + b3 * c3 };
    const double A[4] = { -a0 - a1 * c - a2 * c2 - a3 * c3, -3 * a0 - a1 * c + a2 * c2 + 3 * a3 * c3,
                          -3 * a0 + a1 * c + a2 * c2 - 3 * a3 * c3, -a0 + a1 * c - a2 * c2 + a3 * c3 };
    for (int i = 0; i < 4; i++){
        b[i] = B[i] / A[0];
        a[i] = A[i] / A[0];
    }
}

double responseMagnitude(const double b[4], const double a[4], double frequency, double rate){
    const std::complex<double> z = std::polar(1.0, -2.0 * PI_D * frequency / rate); // z^-1
    std::complex<double> top = 0.0, bottom = 0.0, power = 1.0;
    for (int i = 0; i < 4; i++){
        top += b[i] * power;
        bottom += a[i] * power;
        power *= z;
    }
    return std::abs(top / bottom);
}

double processToneStack(AmpState& amp, double x){
    // Transposed direct form II, in doubles: the poles sit close to 1 at four times the rate
    const double y = amp.stackB[0] * x + amp.stackZ[0];
    amp.stackZ[0] = amp.stackB[1] * x - amp.stackA[1] * y + amp.stackZ[1];
    amp.stackZ[1] = amp.stackB[2] * x - amp.stackA[2] * y + amp.stackZ[2];
    amp.stackZ[2] = amp.stackB[3] * x - amp.stackA[3] * y;
    for (double& z : amp.stackZ) if (std::fabs(z) < 1e-30) z = 0.0;
    return y;
}

} // namespace

void setOversampler(Oversampler& oversampler, float rate){
    const float fast = rate * OVERSAMPLING, cutoff = std::min(rate * 0.45f, 20000.0f);
    for (int i = 0; i < 4; i++){
        setLowPass(oversampler.up[i], cutoff, BUTTERWORTH_Q[i], fast);
        setLowPass(oversampler.down[i], cutoff, BUTTERWORTH_Q[i], fast);
    }
}

int ampModelCount(){ return AMP_COUNT; }

const char* const* ampModelNames(){
    static const char* names[AMP_COUNT];
    for (int i = 0; i < AMP_COUNT; i++) names[i] = AMPS[i].name;
    return names;
}

float toneStackDb(int model, float treble, float mid, float bass, float frequency, float rate){
    double b[4], a[4];
    toneStackCoefficients(ampDesign(model), treble, mid, bass, rate, b, a);
    return (float)(20.0 * std::log10(std::max(responseMagnitude(b, a, frequency, rate), 1e-12)));
}

void setUpAmp(AmpState& amp, const float* v, float rate){
    const int model = std::clamp((int)std::lround(v[0]), 0, AMP_COUNT - 1);
    const AmpDesign& d = AMPS[model];
    if (model != amp.model){ // another amp: its memory starts afresh
        amp = AmpState{};
        amp.model = model;
    }
    const float gain = std::clamp(v[1], 0.0f, 1.0f), fast = rate * OVERSAMPLING;
    amp.stages = std::clamp(d.stages, 1, MAX_AMP_STAGES);
    amp.stageGain = d.gainLow * std::pow(d.gainHigh / d.gainLow, gain); // a log pot
    amp.bias = d.bias;
    amp.inputTrim = INPUT_TRIM;
    amp.powerDrive = d.powerDrive;
    amp.sag = d.sag;
    amp.cleanBlend = d.cleanBlend;
    amp.level = d.level;
    setHighPass(amp.tight, d.tightHz, 0.7f, rate);
    setShelf(amp.bright, 2500.0f, d.brightDb * (1.0f - gain), true, rate);
    setOversampler(amp.oversampler, rate);
    for (int s = 0; s < MAX_AMP_STAGES; s++){
        setOnePole(amp.coupling[s], d.couplingHz, fast);
        setOnePole(amp.miller[s], d.millerHz, fast);
    }
    toneStackCoefficients(d, v[4], v[3], v[2], fast, amp.stackB, amp.stackA);
    double b[4], a[4];
    toneStackCoefficients(d, 0.5f, 0.5f, 0.5f, fast, b, a);
    amp.stackMakeup = (float)(1.0 / std::max(responseMagnitude(b, a, 1000.0, fast), 1e-6)); // its loss made up, knobs at noon
    amp.sagAttack = 1.0f - std::exp(-1.0f / (0.005f * fast));
    amp.sagRelease = 1.0f - std::exp(-1.0f / (0.12f * fast));
    setShelf(amp.presence, d.presenceHz, -6.0f + 15.0f * std::clamp(v[5], 0.0f, 1.0f), true, rate);
    setPeak(amp.depth, d.depthHz, 0.8f, d.depthDb, rate);
    setLowPass(amp.cleanLow, 250.0f, 0.7f, rate);
}

float processAmp(AmpState& amp, float x){
    const float in = amp.bright.process(amp.tight.process(x * amp.inputTrim));
    float y = oversampled(amp.oversampler, in, [&](float u){
        // The preamp's tubes, one after the other
        for (int s = 0; s < amp.stages; s++){
            u = softClip(amp.stageGain * u + amp.bias) - softClip(amp.bias);
            u = lowPassOne(amp.miller[s], highPassOne(amp.coupling[s], u));
        }
        // The tone stack, then the power amp: its supply sagging with the playing
        u = (float)processToneStack(amp, u) * amp.stackMakeup;
        const float level = std::fabs(u);
        amp.sagEnvelope += (level - amp.sagEnvelope) * (level > amp.sagEnvelope ? amp.sagAttack : amp.sagRelease);
        const float headroom = 1.0f / (1.0f + amp.sag * amp.sagEnvelope * 2.0f);
        return softClip(amp.powerDrive * u / headroom) * headroom;
    });
    y = amp.depth.process(amp.presence.process(y)) * amp.level;
    if (amp.cleanBlend > 0.0f) y += amp.cleanLow.process(x) * amp.cleanBlend * 2.0f;
    return y;
}

const char* const* driveTypeNames(){
    static const char* names[] = { "Overdrive", "Distortion", "Fuzz" };
    return names;
}

void setUpDrive(DriveState& drive, const float* v, float rate){
    const int type = std::clamp((int)std::lround(v[1]), 0, (int)DriveType::Count - 1);
    if (type != drive.type){
        drive = DriveState{};
        drive.type = type;
    }
    const float amount = std::clamp(v[0], 0.0f, 1.0f), tone = std::clamp(v[2], 0.0f, 1.0f), fast = rate * OVERSAMPLING;
    setOversampler(drive.oversampler, rate);
    drive.level = v[3] * 1.4f;
    drive.blend = v[4];
    switch ((DriveType)type){
        case DriveType::Overdrive:
            // A Tube Screamer: the clipping hears only what's above 720 Hz, so the lows pass clean (its mid hump)
            drive.gain = 1.0f + 80.0f * amount * amount;
            setHighPass(drive.preHigh, 720.0f, 0.5f, fast);
            setLowPass(drive.toneLow, 700.0f * std::pow(9.0f, tone), 0.6f, fast);
            break;
        case DriveType::Distortion:
            // A RAT: up to 60 dB of an op-amp's gain, its bandwidth falling as the gain rises, into hard diodes
            drive.gain = 1.0f + 1000.0f * amount * amount;
            setHighPass(drive.preHigh, 60.0f, 0.6f, fast);
            setLowPass(drive.preLow, std::min(20000.0f, 1.0e6f / drive.gain), 0.6f, fast);
            setLowPass(drive.toneLow, 500.0f * std::pow(24.0f, tone), 0.6f, fast);
            break;
        default:
            // A Big Muff: two clipping stages, then its tone: the lows or the highs, the mids scooped between
            drive.gain = 4.0f + 60.0f * amount;
            setOnePole(drive.stageHigh[0], 100.0f, fast);
            setOnePole(drive.stageHigh[1], 100.0f, fast);
            setLowPass(drive.preLow, 7000.0f, 0.6f, fast);
            setLowPass(drive.toneLow, 400.0f, 0.6f, fast);
            setHighPass(drive.toneHigh, 1200.0f, 0.6f, fast);
            drive.toneMix = tone;
            break;
    }
}

float processDrive(DriveState& drive, float x){
    const float wet = oversampled(drive.oversampler, x, [&](float u){
        switch ((DriveType)drive.type){
            case DriveType::Overdrive: {
                const float clipped = softClip(drive.gain * drive.preHigh.process(u)) * 0.5f;
                return drive.toneLow.process(u + clipped);
            }
            case DriveType::Distortion: {
                float driven = drive.preLow.process(drive.gain * drive.preHigh.process(u));
                driven = driven / std::pow(1.0f + std::pow(std::fabs(driven), 4.0f), 0.25f) * 0.18f; // silicon diodes: a hard knee
                return drive.toneLow.process(driven);
            }
            default: {
                float driven = softClip(drive.gain * highPassOne(drive.stageHigh[0], u));
                driven = softClip(12.0f * highPassOne(drive.stageHigh[1], driven));
                driven = drive.preLow.process(driven);
                return (drive.toneLow.process(driven) * (1.0f - drive.toneMix) + drive.toneHigh.process(driven) * drive.toneMix) * 0.3f;
            }
        }
    });
    return wet * drive.level * drive.blend + x * (1.0f - drive.blend);
}
