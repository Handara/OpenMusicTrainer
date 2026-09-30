#include "doctest/doctest.h"

#include "core/tonechain.h"
#include "core/tonelibrary.h"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <random>
#include <vector>

TEST_CASE("a tone survives being written and read back"){
    Tone tone;
    tone.name = "My growl, v2";
    tone.volume = 0.65f;
    tone.effects = { makeEffect(EffectType::Octaver), makeEffect(EffectType::Drive), makeEffect(EffectType::Reverb) };
    tone.effects[1].values[0] = 0.8f;
    tone.effects[2].on = false;
    Tone read;
    std::string error;
    REQUIRE_MESSAGE(readTone(writeTone(tone), read, error), error);
    CHECK(read.name == "My growl, v2");
    CHECK(read.volume == doctest::Approx(0.65f));
    REQUIRE(read.effects.size() == 3);
    CHECK(read.effects[0].type == EffectType::Octaver);
    CHECK(read.effects[1].values[0] == doctest::Approx(0.8f));
    CHECK_FALSE(read.effects[2].on);
}

TEST_CASE("tone files are read leniently, and only tone files"){
    Tone tone;
    std::string error;
    REQUIRE(readTone("lahn_tone 1\r\n"
                     "name Odd\r\n"
                     "flanger on rate 3\r\n"               // an effect from a newer lahn: skipped
                     "drive on drive 7 sparkle 1\r\n"       // out of range, and a setting it doesn't know
                     "amp\r\n", tone, error));              // no settings at all: its standard ones
    REQUIRE(tone.effects.size() == 2);
    CHECK(tone.effects[0].values[0] == doctest::Approx(1.0f)); // kept in range
    CHECK(tone.effects[1].values[0] == doctest::Approx(makeEffect(EffectType::Amp).values[0]));
    CHECK_FALSE(readTone("version 1\nnote_view neck\n", tone, error)); // a settings file isn't a tone
    CHECK(error == "not a lahn tone");
    CHECK_FALSE(readTone("", tone, error));
}

TEST_CASE("the built-in tones: Clean first, every one of them readable"){
    REQUIRE(!builtInTones().empty());
    CHECK(builtInTones().front().name == "Clean");
    for (const Tone& tone : builtInTones()){
        CAPTURE(tone.name);
        CHECK((int)tone.effects.size() <= MAX_EFFECTS);
        Tone read;
        std::string error;
        CHECK(readTone(writeTone(tone), read, error));
        CHECK(read.effects.size() == tone.effects.size());
    }
    CHECK(findBuiltInTone("Dub") != nullptr);
    CHECK(findBuiltInTone("Nope") == nullptr);
}

// A note plucked: a decaying 110 Hz with harmonics
static std::vector<float> pluck(int rate, float seconds, float gain){
    std::vector<float> samples((size_t)(rate * seconds));
    for (size_t i = 0; i < samples.size(); i++){
        float t = (float)i / rate;
        samples[i] = gain * std::exp(-t * 2.0f) * (std::sin(2 * 3.14159f * 110 * t) + 0.5f * std::sin(2 * 3.14159f * 220 * t));
    }
    return samples;
}

TEST_CASE("no effect delays the sound: what's played is heard at once"){
    const int rate = 48000;
    for (int type = 0; type < (int)EffectType::Count; type++){
        CAPTURE(effectInfo((EffectType)type).name);
        ToneChain chain;
        initToneChain(chain, rate);
        Tone tone;
        tone.effects = { makeEffect((EffectType)type) };
        tone.volume = 1.0f;
        setToneChain(chain, toneParameters(tone));
        // A step: silence, then the string's attack. Its very first sample must come out.
        std::vector<float> samples(64, 0.0f);
        for (int i = 32; i < 64; i++) samples[i] = 0.3f;
        processToneChain(chain, samples.data(), 64);
        CHECK(std::fabs(samples[32]) > 0.01f);
    }
}

TEST_CASE("every effect turned all the way up stays finite and in range, and the amp's knobs do something"){
    const int rate = 44100;
    std::mt19937 random(3);
    std::uniform_real_distribution<float> noise(-1.0f, 1.0f);
    Tone loud;
    loud.volume = 1.0f;
    for (int type = 0; type < (int)EffectType::Count; type++){
        Effect effect = makeEffect((EffectType)type);
        const EffectInfo& info = effectInfo(effect.type);
        for (int p = 0; p < info.parameterCount; p++) effect.values[p] = info.parameters[p].max;
        loud.effects.push_back(effect);
    }
    ToneChain chain;
    initToneChain(chain, rate);
    setToneChain(chain, toneParameters(loud));
    std::vector<float> samples(rate);
    for (float& sample : samples) sample = noise(random);
    processToneChain(chain, samples.data(), (int)samples.size());
    for (float sample : samples){
        REQUIRE(std::isfinite(sample));
        REQUIRE(std::fabs(sample) <= 1.0f);
    }

    // Bass all the way up, then all the way down: the low note comes out louder with it up
    auto energyWithBass = [&](float bass){
        ToneChain amp;
        initToneChain(amp, rate);
        Tone tone;
        tone.volume = 1.0f;
        tone.effects = { makeEffect(EffectType::Amp) };
        tone.effects[0].values[1] = bass;
        setToneChain(amp, toneParameters(tone));
        std::vector<float> note = pluck(rate, 0.5f, 0.2f);
        processToneChain(amp, note.data(), (int)note.size());
        float energy = 0.0f;
        for (float sample : note) energy += sample * sample;
        return energy;
    };
    CHECK(energyWithBass(12.0f) > 2.0f * energyWithBass(-12.0f));
}

TEST_CASE("a knob turned while playing keeps the effect's memory: the echo already on its way still comes"){
    const int rate = 48000;
    ToneChain chain;
    initToneChain(chain, rate);
    Tone tone;
    tone.volume = 1.0f;
    tone.effects = { makeEffect(EffectType::Delay) };
    tone.effects[0].values[0] = 100.0f; // 100 ms
    tone.effects[0].values[2] = 1.0f;   // the echo as loud as the note
    setToneChain(chain, toneParameters(tone));
    std::vector<float> samples(rate / 5, 0.0f);
    samples[0] = 0.5f;
    processToneChain(chain, samples.data(), rate / 20); // the note, 50 ms of it
    tone.effects[0].values[1] = 0.1f;                   // feedback changed mid-way
    setToneChain(chain, toneParameters(tone));
    processToneChain(chain, samples.data() + rate / 20, (int)samples.size() - rate / 20);
    float echo = 0.0f;
    for (int i = rate / 10 - 50; i < rate / 10 + 50; i++) echo = std::max(echo, std::fabs(samples[i]));
    CHECK(echo > 0.2f);
}

TEST_CASE("the player's tones: saved, found, named apart, imported, deleted"){
    namespace fs = std::filesystem;
    fs::path folder = fs::temp_directory_path() / "lahn_tests" / "tones";
    fs::remove_all(folder);
    std::vector<std::string> problems;
    CHECK(loadUserTones(folder.string(), problems).empty()); // no folder yet: no tones, no trouble
    CHECK(problems.empty());

    Tone mine = *findBuiltInTone("Growl");
    mine.name = "Sunday: growl";
    std::string error;
    REQUIRE_MESSAGE(saveUserTone(folder.string(), mine, error), error);
    std::ofstream(folder / "broken.tone") << "hello";
    std::vector<Tone> tones = loadUserTones(folder.string(), problems);
    REQUIRE(tones.size() == 1);
    CHECK(tones[0].name == "Sunday: growl");
    CHECK(problems.size() == 1); // the broken file, said why

    CHECK(freeToneName("Clean", tones) == "Clean 2");  // a built-in's name is taken
    CHECK(freeToneName("Sunday: growl", tones) == "Sunday: growl 2");
    CHECK(freeToneName("Fresh", tones) == "Fresh");
    CHECK(findTone("Sunday: growl", tones).effects.size() == mine.effects.size());
    CHECK(findTone("Dub", tones).name == "Dub");
    CHECK(findTone("gone", tones).name == "Clean");

    // A friend's tone of the same name comes in beside it
    fs::path shared = fs::temp_directory_path() / "lahn_tests" / "shared.tone";
    { std::ofstream(shared) << writeTone(mine); }
    Tone imported;
    REQUIRE_MESSAGE(importTone(shared.string(), folder.string(), tones, imported, error), error);
    CHECK(imported.name == "Sunday: growl 2");
    CHECK(loadUserTones(folder.string(), problems).size() == 2);

    REQUIRE(deleteUserTone(folder.string(), "Sunday: growl", error));
    std::vector<Tone> left = loadUserTones(folder.string(), problems);
    REQUIRE(left.size() == 1);
    CHECK(left[0].name == "Sunday: growl 2");
}
