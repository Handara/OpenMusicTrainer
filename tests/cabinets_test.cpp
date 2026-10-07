#include "doctest/doctest.h"

#include "core/cabinets.h"
#include "core/tonechain.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
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

// A 32-bit float WAV, as impulse responses often come
static void writeFloatWav(const std::string& path, const std::vector<float>& samples, int rate){
    std::ofstream out(path, std::ios::binary);
    auto put32 = [&](uint32_t value){ out.write((const char*)&value, 4); };
    auto put16 = [&](uint16_t value){ out.write((const char*)&value, 2); };
    out.write("RIFF", 4);
    put32(36 + (uint32_t)samples.size() * 4);
    out.write("WAVEfmt ", 8);
    put32(16);
    put16(3); // floats
    put16(1);
    put32((uint32_t)rate);
    put32((uint32_t)rate * 4);
    put16(4);
    put16(32);
    out.write("data", 4);
    put32((uint32_t)samples.size() * 4);
    out.write((const char*)samples.data(), (std::streamsize)(samples.size() * 4));
}

TEST_CASE("cabinets: a player's impulse response file plays like a built-in one"){
    const std::string path = (std::filesystem::temp_directory_path() / "lahn-test-cabinet.wav").string();
    // 2 ms of silence first (the mic's distance), then a cabinet's response, at 44.1 kHz, too loud
    std::vector<float> file(88, 0.0f);
    for (float tap : cabinetResponse(3, 44100)) file.push_back(tap * 3.0f);
    writeFloatWav(path, file, 44100);
    std::string error;
    const ToneAsset* asset = cabinetFromFile(path, error);
    REQUIRE_MESSAGE(asset != nullptr, error);
    REQUIRE(asset->responses.size() == 4);
    const ToneAsset::Response* at48 = asset->forRate(48000);
    std::vector<float> taps(at48->reversed.rbegin(), at48->reversed.rend());
    CHECK(taps.size() == 2048);
    // The silence is gone: it starts at once
    float peak = 0.0f;
    for (float tap : taps) peak = std::max(peak, std::fabs(tap));
    float early = 0.0f;
    for (int i = 0; i < 8; i++) early = std::max(early, std::fabs(taps[i]));
    CHECK(early > 0.3f * peak);
    // As loud as a built-in one, and sounding as it did
    for (float f : { 300.0f, 1000.0f, 2500.0f }) CHECK(std::fabs(responseDb(taps, f, 48000) - cabinetDb(3, f)) < 1.0f);
    // Read once: asked again, the same
    CHECK(cabinetFromFile(path, error) == asset);
    std::filesystem::remove(path);
    // Not there: why, and nothing to play
    CHECK(cabinetFromFile(path + ".missing.wav", error) == nullptr);
    CHECK_FALSE(error.empty());
}
