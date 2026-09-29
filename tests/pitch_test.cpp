#include "doctest/doctest.h"

#include "core/pitch.h"

#include <cmath>
#include <random>
#include <vector>

const int SAMPLE_RATE = 48000;
const double PI = 3.14159265358979323846;

static float centsBetween(float frequency, float reference){
    return 1200.0f * std::log2(frequency / reference);
}

static std::vector<float> sine(float frequency, int count){
    std::vector<float> samples(count);
    for (int i = 0; i < count; i++) samples[i] = 0.5f * (float)std::sin(2 * PI * frequency * i / SAMPLE_RATE);
    return samples;
}

// Karplus-Strong plucked string: harmonics like a real guitar, which is what makes octave errors possible.
// Its true pitch is SAMPLE_RATE / (period - 0.5): the averaging step adds half a sample of delay.
static std::vector<float> pluck(int period, int count, std::mt19937& rng){
    std::uniform_real_distribution<float> noise(-1.0f, 1.0f);
    std::vector<float> line(period);
    for (float& x : line) x = noise(rng);
    std::vector<float> samples;
    int skip = SAMPLE_RATE / 10; // let the initial noise burst settle, like a real pluck after the attack
    for (int i = 0; i < count + skip; i++){
        int k = i % period;
        if (i >= skip) samples.push_back(line[k]);
        line[k] = 0.996f * 0.5f * (line[k] + line[(k + 1) % period]);
    }
    return samples;
}

TEST_CASE("pitch detection is accurate across guitar and bass range"){
    PitchDetector detector;
    initPitchDetector(detector, SAMPLE_RATE, 30.0f, 1400.0f);
    const int count = pitchWindowSize(detector);

    SUBCASE("pure tones"){
        for (float frequency : {30.87f, 110.0f, 440.0f, 1046.5f}){
            PitchResult result = detectPitch(detector, sine(frequency, count).data(), count);
            CHECK_MESSAGE(std::fabs(centsBetween(result.frequency, frequency)) < 1.0f, frequency);
        }
    }
    SUBCASE("plucked strings, no octave errors"){
        std::mt19937 rng(1);
        for (float nominal : {41.2f, 82.41f, 110.0f, 196.0f, 329.63f, 1046.5f}){ // E1 E2 A2 G3 E4 C6
            int period = (int)std::round(SAMPLE_RATE / nominal);
            float truth = SAMPLE_RATE / (period - 0.5f);
            PitchResult result = detectPitch(detector, pluck(period, count, rng).data(), count);
            CHECK_MESSAGE(std::fabs(centsBetween(result.frequency, truth)) < 1.0f, nominal);
            CHECK(result.clarity > 0.9f);
        }
    }
}

TEST_CASE("noise and too-short input give no pitch"){
    PitchDetector detector;
    initPitchDetector(detector, SAMPLE_RATE, 30.0f, 1400.0f);
    const int count = pitchWindowSize(detector);

    std::mt19937 rng(2);
    std::normal_distribution<float> gaussian(0.0f, 0.2f);
    std::vector<float> noise(count);
    for (float& x : noise) x = gaussian(rng);
    CHECK(detectPitch(detector, noise.data(), count).frequency == 0.0f);

    std::vector<float> tooShort = sine(440.0f, detector.maxLag / 2);
    CHECK(detectPitch(detector, tooShort.data(), (int)tooShort.size()).frequency == 0.0f);
}

TEST_CASE("a note whose octave overtone is louder than it isn't read an octave high"){
    // A bass through its pickups: the second harmonic often far stronger than the fundamental lines up well enough
    // at half the period to pass first. A thousand string-like notes across a bass's range, each with overtones of
    // random strength, a little inharmonicity (overtones slightly sharp, as on real strings), decay and noise: before
    // the octave check, about 2 in 100 read an octave high.
    const int rate = 48000;
    PitchDetector detector;
    initPitchDetector(detector, rate, 37.0f, 1400.0f);
    const int count = pitchWindowSize(detector);
    std::vector<float> samples(count);
    std::mt19937 rng(11);
    std::uniform_real_distribution<float> uni(0.0f, 1.0f);
    int octaveOff = 0;
    for (int note = 0; note < 1000; note++){
        double f = 41.4 * std::pow(2.0, uni(rng) * 4.0);
        double h1 = 0.3 + uni(rng), h2 = uni(rng) * 2.0, h3 = uni(rng), h4 = uni(rng) * 0.5;
        double stretch = 1.0 + uni(rng) * 0.004, noise = uni(rng) * 0.15, decay = uni(rng) * 8.0;
        for (int i = 0; i < count; i++){
            double t = (double)i / rate, w = 2 * 3.14159265 * f * t;
            double v = h1 * std::sin(w) + h2 * std::sin(2 * stretch * w) + h3 * std::sin(3 * stretch * stretch * w)
                     + h4 * std::sin(4 * stretch * stretch * stretch * w);
            samples[i] = (float)(std::exp(-decay * t) * v + noise * (uni(rng) * 2 - 1));
        }
        PitchResult result = detectPitch(detector, samples.data(), count);
        if (result.frequency > 0.0f && std::fabs(std::fabs(12.0 * std::log2(result.frequency / f)) - 12.0) < 0.5) octaveOff++;
    }
    CHECK(octaveOff == 0);
}
