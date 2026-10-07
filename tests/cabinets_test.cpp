#include "doctest/doctest.h"

#include "core/cabinets.h"
#include "core/tonechain.h"

#include <algorithm>
#include <cmath>
#include <random>
#include <string>
#include <vector>

// An impulse response's gain at a frequency, in dB (straight from its samples)
static float responseDb(const std::vector<float>& taps, float frequency, int rate){
    double re = 0.0, im = 0.0;
    for (size_t i = 0; i < taps.size(); i++){
        const double angle = 2.0 * 3.14159265358979323846 * frequency * (double)i / rate;
        re += taps[i] * std::cos(angle);
        im -= taps[i] * std::sin(angle);
    }
    return (float)(10.0 * std::log10(re * re + im * im + 1e-30));
}

TEST_CASE("cabinets: a speaker's lows start, its top falls away, its loudest at 0 dB"){
    for (int cabinet = 0; cabinet < cabinetCount(); cabinet++){
        const std::string name = cabinetNames()[cabinet];
        CAPTURE(name);
        float peak = -1000.0f;
        for (float f = 50.0f; f <= 8000.0f; f *= 1.05f) peak = std::max(peak, cabinetDb(cabinet, f));
        CHECK(std::fabs(peak) < 0.25f); // found on another grid than the design's
        CHECK(cabinetDb(cabinet, 12000.0f) < -15.0f); // no fizz
        CHECK(cabinetDb(cabinet, 20.0f) < -6.0f);
    }
    // A bass cabinet keeps the lows a guitar's loses
    CHECK(cabinetDb(4, 50.0f) > cabinetDb(2, 50.0f) + 6.0f);
}

TEST_CASE("cabinets: the impulse response sounds as designed, and starts at once"){
    const int rate = 48000;
    for (int cabinet = 0; cabinet < cabinetCount(); cabinet++){
        const std::string name = cabinetNames()[cabinet];
        CAPTURE(name);
        const std::vector<float> taps = cabinetResponse(cabinet, rate);
        CHECK(taps.size() == 2048);
        for (float f : { 300.0f, 1000.0f, 2500.0f }) CHECK(std::fabs(responseDb(taps, f, rate) - cabinetDb(cabinet, f)) < 1.0f);
        // Minimum phase: its energy at the front, nothing waiting to come
        double front = 0.0, all = 0.0;
        for (size_t i = 0; i < taps.size(); i++){
            all += taps[i] * taps[i];
            if (i < taps.size() / 8) front += taps[i] * taps[i];
        }
        CHECK(front > 0.95 * all);
    }
}

TEST_CASE("cabinets: a response taken to another rate keeps its sound"){
    const std::vector<float> taps = cabinetResponse(2, 48000);
    const std::vector<float> resampled = resampleResponse(taps, 48000, 44100);
    CHECK(resampled.size() == (size_t)std::ceil(taps.size() * 44100.0 / 48000.0));
    for (float f : { 200.0f, 1000.0f, 3000.0f }) CHECK(std::fabs(responseDb(resampled, f, 44100) - responseDb(taps, f, 48000)) < 0.5f);
}

TEST_CASE("cabinets: the chain plays through the response with no delay, then its filters"){
    const int rate = 48000;
    ToneAsset asset;
    asset.responses.push_back({ rate, { 0.25f, 0.5f, 1.0f } }); // 1, 0.5, 0.25: back to front
    Tone tone;
    tone.volume = 1.0f;
    tone.effects = { makeEffect(EffectType::Cabinet) };
    ToneParameters with = toneParameters(tone);
    with.assets[0] = &asset;
    ToneChain convolved, plain;
    initToneChain(convolved, rate);
    initToneChain(plain, rate);
    setToneChain(convolved, with);
    setToneChain(plain, toneParameters(tone)); // no response: the filters alone
    std::vector<float> impulse(64, 0.0f), echoes(64, 0.0f);
    impulse[0] = 0.5f;
    echoes[0] = 0.5f;
    echoes[1] = 0.25f;
    echoes[2] = 0.125f;
    processToneChain(convolved, impulse.data(), 64);
    processToneChain(plain, echoes.data(), 64);
    CHECK(std::fabs(impulse[0]) > 0.1f); // heard at once
    for (int i = 0; i < 64; i++) CHECK(impulse[i] == doctest::Approx(echoes[i]).epsilon(1e-3));
}

TEST_CASE("cabinets: every built-in one is ready for each rate a device may run at"){
    Tone tone;
    tone.effects = { makeEffect(EffectType::Compressor), makeEffect(EffectType::Cabinet) };
    ToneParameters parameters = toneParameters(tone);
    attachCabinets(parameters);
    CHECK(parameters.assets[0] == nullptr);
    REQUIRE(parameters.assets[1] != nullptr);
    CHECK(parameters.assets[1]->forRate(44100)->sampleRate == 44100);
    CHECK(parameters.assets[1]->forRate(96000)->reversed.size() == 4096);
    CHECK(parameters.assets[1]->forRate(192000)->sampleRate == 96000);
}
