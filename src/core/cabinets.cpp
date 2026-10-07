#include "core/cabinets.h"

#include "core/fft.h"

#include <algorithm>
#include <cmath>
#include <memory>

namespace {

const double PI_D = 3.14159265358979323846;
const float RESPONSE_S = 2048.0f / 48000.0f; // how long a built-in cabinet's response is: 2048 taps at 48 kHz
const float FLOOR_DB = -120.0f;               // nothing designed quieter (the logarithm needs something)
const int ASSET_RATES[] = { 44100, 48000, 88200, 96000 };

// A peak or a dip in the response: the cone's modes and the box's
struct CabinetBump {
    float hz, db, q;
};

// A speaker in its cabinet, as its response: where its lows start (a resonance, then 12 dB an octave down), an open
// back losing more of them, where its cone stops following the highs (24 dB an octave, then steeper), its peaks and
// dips, a horn for the top (bass cabinets), and the small ripples the box and the room add (reflections)
struct CabinetDesign {
    const char* name;
    float lowHz, lowQ, openBack;
    float highHz, highQ;
    CabinetBump bumps[6];
    float hornDb; // the horn's level against the speakers; below -90 for none
    unsigned seed; // its ripples
};

const CabinetDesign DESIGNS[] = {
    // Guitar
    { "1x12 Open", 95.0f, 0.8f, 0.8f, 5800.0f, 0.9f,
      { { 200, -1.5f, 1.2f }, { 550, -2.0f, 1.0f }, { 1800, 2.0f, 2.0f }, { 3200, 3.5f, 3.0f }, { 4600, 2.0f, 4.0f }, { 7000, -6.0f, 3.0f } }, -100.0f, 11 },
    { "2x12 Alnico", 90.0f, 0.9f, 0.5f, 5200.0f, 1.1f,
      { { 120, 1.0f, 1.5f }, { 800, -1.5f, 1.5f }, { 1400, 2.5f, 2.0f }, { 2600, 4.0f, 2.5f }, { 4200, 3.0f, 4.0f }, { 6500, -8.0f, 3.0f } }, -100.0f, 23 },
    { "4x12 Modern", 85.0f, 1.4f, 0.0f, 5000.0f, 1.3f,
      { { 130, 2.0f, 1.2f }, { 420, -2.5f, 1.2f }, { 1300, 1.5f, 1.5f }, { 2600, 5.0f, 2.5f }, { 3900, 3.0f, 3.0f }, { 6200, -10.0f, 2.5f } }, -100.0f, 37 },
    { "4x12 Vintage", 80.0f, 1.3f, 0.0f, 4300.0f, 1.2f,
      { { 150, 1.5f, 1.2f }, { 700, -1.0f, 1.0f }, { 1800, 3.0f, 1.8f }, { 2900, 2.0f, 3.0f }, { 3700, -3.0f, 4.0f }, { 5600, -8.0f, 2.0f } }, -100.0f, 41 },
    // Bass
    { "8x10 Bass", 48.0f, 1.0f, 0.0f, 4200.0f, 0.9f,
      { { 80, 2.0f, 1.0f }, { 250, -1.5f, 1.0f }, { 800, 1.5f, 1.2f }, { 1800, 2.0f, 2.0f }, { 3000, 1.5f, 3.0f }, { 5500, -6.0f, 2.0f } }, -100.0f, 53 },
    { "4x10 Bass + horn", 45.0f, 0.9f, 0.0f, 3800.0f, 0.9f,
      { { 70, 1.0f, 1.0f }, { 400, -2.0f, 1.0f }, { 1200, 1.5f, 1.5f }, { 2700, -2.5f, 2.0f }, { 5000, 1.5f, 2.0f }, { 9000, -3.0f, 2.0f } }, -8.0f, 67 },
    { "1x15 Bass", 40.0f, 1.1f, 0.0f, 2600.0f, 0.9f,
      { { 60, 2.5f, 1.2f }, { 350, -1.0f, 1.0f }, { 900, 1.0f, 1.5f }, { 1600, 2.0f, 2.0f }, { 2200, -2.0f, 3.0f }, { 4000, -6.0f, 2.0f } }, -100.0f, 79 },
};
const int DESIGN_COUNT = (int)(sizeof DESIGNS / sizeof DESIGNS[0]);

// A second-order high-pass's and low-pass's gain at `ratio` = frequency / its corner
double highPassGain(double ratio, double q){
    const double r2 = ratio * ratio;
    return r2 / std::sqrt((1.0 - r2) * (1.0 - r2) + (ratio / q) * (ratio / q));
}
double lowPassGain(double ratio, double q){
    const double r2 = ratio * ratio;
    return 1.0 / std::sqrt((1.0 - r2) * (1.0 - r2) + (ratio / q) * (ratio / q));
}

// The response as designed, before it's brought to 0 dB at its loudest
double designedDb(const CabinetDesign& design, double frequency){
    const double f = std::max(frequency, 1.0);
    double speaker = highPassGain(f / design.lowHz, design.lowQ);
    if (design.openBack > 0.0f){
        const double r = f / (design.lowHz * 1.3);
        speaker *= std::pow(r / std::sqrt(1.0 + r * r), design.openBack); // the back's own cancellation, below
    }
    speaker *= lowPassGain(f / design.highHz, design.highQ) * lowPassGain(f / design.highHz, 0.6) * lowPassGain(f / (design.highHz * 1.7), 0.7);
    double total = speaker;
    if (design.hornDb > -90.0f){
        const double horn = std::pow(10.0, design.hornDb / 20.0) * highPassGain(f / 3000.0, 0.7) * lowPassGain(f / 11000.0, 0.7);
        total = std::sqrt(speaker * speaker + horn * horn);
    }
    double db = 20.0 * std::log10(std::max(total, 1e-9));
    for (const CabinetBump& bump : design.bumps){
        const double off = bump.q * (f / bump.hz - bump.hz / f);
        db += bump.db / (1.0 + off * off);
    }
    // The box's and the room's reflections: a few short echoes, rippling the response above the lows
    unsigned state = design.seed * 2654435761u + 12345u;
    auto random = [&](){ state = state * 1664525u + 1013904223u; return (double)(state >> 8) / (double)(1u << 24); };
    double re = 1.0, im = 0.0;
    for (int i = 0; i < 3; i++){
        const double delay = 0.0008 + 0.0024 * random(), gain = 0.07 + 0.1 * random();
        re += gain * std::cos(2.0 * PI_D * f * delay);
        im -= gain * std::sin(2.0 * PI_D * f * delay);
    }
    const double above = std::clamp((f - 600.0) / 1400.0, 0.0, 1.0);
    db += above * above * (3.0 - 2.0 * above) * 10.0 * std::log10(re * re + im * im);
    return std::max(db, (double)FLOOR_DB);
}

// How far below 0 dB its loudest is, over what a speaker plays
double designedPeakDb(const CabinetDesign& design){
    double peak = -1e9;
    for (int i = 0; i <= 240; i++) peak = std::max(peak, designedDb(design, 50.0 * std::pow(8000.0 / 50.0, i / 240.0)));
    return peak;
}

} // namespace

int cabinetCount(){ return DESIGN_COUNT; }

const char* const* cabinetNames(){
    static const char* names[DESIGN_COUNT];
    for (int i = 0; i < DESIGN_COUNT; i++) names[i] = DESIGNS[i].name;
    return names;
}

float cabinetDb(int cabinet, float frequency){
    const CabinetDesign& design = DESIGNS[std::clamp(cabinet, 0, DESIGN_COUNT - 1)];
    return (float)std::max(designedDb(design, frequency) - designedPeakDb(design), (double)FLOOR_DB);
}

// The impulse response that has the response designed and starts at once (minimum phase), by way of its cepstrum:
// the logarithm of its gains, its echoes folded to the front, and back
std::vector<float> cabinetResponse(int cabinet, int sampleRate){
    const CabinetDesign& design = DESIGNS[std::clamp(cabinet, 0, DESIGN_COUNT - 1)];
    const double peak = designedPeakDb(design);
    const size_t n = sampleRate > 50000 ? 32768 : 16384;
    const std::vector<std::complex<float>> twiddles = fftTwiddles((int)n);
    std::vector<std::complex<float>> data(n);
    for (size_t k = 0; k <= n / 2; k++){
        const double db = std::max(designedDb(design, (double)k * sampleRate / (double)n) - peak, (double)FLOOR_DB);
        data[k] = (float)(db / 20.0 * std::log(10.0)); // the natural logarithm of the gain
        if (k > 0 && k < n / 2) data[n - k] = data[k];
    }
    const float scale = 1.0f / (float)n; // the inverse isn't scaled
    fft(data, twiddles, true); // the cepstrum
    data[0] *= scale;
    data[n / 2] *= scale;
    for (size_t i = 1; i < n / 2; i++) data[i] *= 2.0f * scale;
    for (size_t i = n / 2 + 1; i < n; i++) data[i] = 0.0f;
    fft(data, twiddles, false);
    for (std::complex<float>& value : data) value = std::exp(value);
    fft(data, twiddles, true);
    for (std::complex<float>& value : data) value *= scale;
    const int length = std::clamp((int)std::lround(RESPONSE_S * sampleRate), 16, MAX_CABINET_TAPS);
    std::vector<float> taps((size_t)length);
    const int fade = length / 4; // the end eased out, so stopping there adds nothing of its own
    for (int i = 0; i < length; i++){
        double value = data[(size_t)i].real();
        if (i >= length - fade) value *= 0.5 + 0.5 * std::cos(PI_D * (i - (length - fade)) / fade);
        taps[(size_t)i] = (float)value;
    }
    return taps;
}

std::vector<float> resampleResponse(const std::vector<float>& taps, int fromRate, int toRate){
    if (fromRate <= 0 || toRate <= 0 || fromRate == toRate || taps.empty()) return taps;
    const double ratio = (double)toRate / fromRate, cutoff = std::min(1.0, ratio);
    const double reach = 16.0 / cutoff; // how far each side the sinc is taken, in the input's samples
    const size_t length = (size_t)std::ceil(taps.size() * ratio);
    std::vector<float> out(length);
    for (size_t m = 0; m < length; m++){
        const double t = m / ratio;
        double sum = 0.0;
        const long first = (long)std::floor(t - reach) + 1, last = (long)std::floor(t + reach);
        for (long i = std::max(0L, first); i <= std::min((long)taps.size() - 1, last); i++){
            const double x = t - i, along = x / reach;
            const double sinc = x == 0.0 ? 1.0 : std::sin(PI_D * cutoff * x) / (PI_D * cutoff * x);
            const double window = 0.42 + 0.5 * std::cos(PI_D * along) + 0.08 * std::cos(2.0 * PI_D * along); // Blackman
            sum += taps[(size_t)i] * cutoff * sinc * window;
        }
        out[m] = (float)(sum / ratio); // as loud at the new rate: fewer samples carry the same sound
    }
    return out;
}

const ToneAsset& builtInCabinet(int cabinet){
    static std::unique_ptr<ToneAsset> made[DESIGN_COUNT];
    const int index = std::clamp(cabinet, 0, DESIGN_COUNT - 1);
    if (!made[index]){
        auto asset = std::make_unique<ToneAsset>();
        for (int rate : ASSET_RATES){
            ToneAsset::Response response;
            response.sampleRate = rate;
            response.reversed = cabinetResponse(index, rate);
            std::reverse(response.reversed.begin(), response.reversed.end());
            asset->responses.push_back(std::move(response));
        }
        made[index] = std::move(asset);
    }
    return *made[index];
}

void attachCabinets(ToneParameters& parameters){
    for (int i = 0; i < parameters.count; i++){
        const Effect& effect = parameters.effects[i];
        parameters.assets[i] = effect.type == EffectType::Cabinet ? &builtInCabinet((int)std::lround(effect.values[0])) : nullptr;
    }
}
