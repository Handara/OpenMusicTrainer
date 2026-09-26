#include "doctest/doctest.h"

#include "core/music.h"
#include "core/notedetector.h"
#include "core/synth.h"

#include <cmath>
#include <random>
#include <vector>

const int RATE = 48000;

struct TestNote { double start; int pitch; };

// A melody played on one string: each note is plucked at its start and cut by the next one (like fretting)
static std::vector<float> playNotes(const std::vector<TestNote>& notes, double totalSeconds, float volume = 1.0f){
    std::vector<float> out((size_t)(totalSeconds * RATE), 0.0f);
    for (size_t i = 0; i < notes.size(); i++){
        size_t from = (size_t)(notes[i].start * RATE);
        size_t to = i + 1 < notes.size() ? (size_t)(notes[i + 1].start * RATE) : out.size();
        std::vector<float> pluck(to - from);
        renderPluck(pluck.data(), (int)pluck.size(), midiToFrequency((float)notes[i].pitch), RATE, (unsigned)i + 1);
        for (size_t j = 0; j < pluck.size(); j++) out[from + j] = pluck[j] * volume;
    }
    return out;
}

static std::vector<DetectedNote> detect(const std::vector<float>& signal, int chunk = 480, NoteDetectorConfig config = {}){
    NoteDetector detector;
    initNoteDetector(detector, RATE, config);
    std::vector<DetectedNote> found;
    for (size_t i = 0; i < signal.size(); i += chunk){
        int count = (int)std::min<size_t>(chunk, signal.size() - i);
        feedNoteDetector(detector, signal.data() + i, count, found);
    }
    return found;
}

static void checkMatches(const std::vector<DetectedNote>& found, const std::vector<TestNote>& expected){
    REQUIRE(found.size() == expected.size());
    for (size_t i = 0; i < expected.size(); i++){
        CHECK_MESSAGE(found[i].pitch == expected[i].pitch, "note " << i);
        double error = found[i].sample / (double)RATE - expected[i].start;
        // Generated plucks start sharply, so the onset must be exact to 1 ms (a hop is 2.7 ms: this checks the refinement)
        CHECK_MESSAGE(std::fabs(error) < 0.001, "note " << i << " onset off by " << error * 1000 << " ms");
    }
}

TEST_CASE("a single plucked note: its pitch and when it started"){
    std::vector<TestNote> notes = {{0.2, 40}}; // guitar low E
    checkMatches(detect(playNotes(notes, 1.0)), notes);
}

TEST_CASE("a melody, including fast notes"){
    std::vector<TestNote> notes = {{0.10, 52}, {0.35, 55}, {0.60, 57}, {0.85, 59},
                                   {0.975, 60}, {1.10, 62}, {1.225, 64}, {1.35, 67}}; // the last five are 16ths at 120 bpm
    checkMatches(detect(playNotes(notes, 2.0)), notes);
}

TEST_CASE("the same note plucked again"){
    std::vector<TestNote> notes = {{0.1, 52}, {0.4, 52}, {0.7, 52}};
    checkMatches(detect(playNotes(notes, 1.2)), notes);
}

TEST_CASE("a hammer-on: a new pitch without a new attack"){
    // A steady tone that changes pitch with no jump in level
    std::vector<float> signal((size_t)(1.0 * RATE), 0.0f);
    double phase = 0.0;
    for (size_t i = (size_t)(0.1 * RATE); i < signal.size(); i++){
        int pitch = i < (size_t)(0.5 * RATE) ? 57 : 60;
        phase += 2 * 3.14159265358979 * midiToFrequency((float)pitch) / RATE;
        signal[i] = 0.3f * (float)std::sin(phase);
    }
    std::vector<DetectedNote> found = detect(signal);
    REQUIRE(found.size() == 2);
    CHECK(found[0].pitch == 57);
    CHECK(found[1].pitch == 60);
    CHECK(std::fabs(found[1].sample / (double)RATE - 0.5) < 0.04); // the change's time is estimated, not exact
}

TEST_CASE("noise, silence and very quiet playing give no notes"){
    std::mt19937 rng(1);
    std::normal_distribution<float> noise(0.0f, 0.2f);
    std::vector<float> signal((size_t)(1.0 * RATE), 0.0f);
    for (size_t i = (size_t)(0.3 * RATE); i < (size_t)(0.6 * RATE); i++) signal[i] = noise(rng); // a scrape or knock
    CHECK(detect(signal).empty());

    CHECK(detect(std::vector<float>(RATE, 0.0f)).empty());
    CHECK(detect(playNotes({{0.2, 52}}, 1.0, 0.001f)).empty()); // -66 dB: below the silence gate
}

TEST_CASE("bass range needs a lower minimum frequency"){
    NoteDetectorConfig bass;
    bass.minFrequency = 30.0f;
    std::vector<TestNote> notes = {{0.2, 28}, {0.6, 33}}; // E1, A1: a 4-string bass's open strings
    checkMatches(detect(playNotes(notes, 1.2), 480, bass), notes);
}

TEST_CASE("the chunk size samples arrive in doesn't change anything"){
    std::vector<float> signal = playNotes({{0.1, 52}, {0.3, 55}, {0.5, 59}}, 1.0);
    std::vector<DetectedNote> big = detect(signal, 4096);
    std::vector<DetectedNote> tiny = detect(signal, 7);
    REQUIRE(big.size() == tiny.size());
    for (size_t i = 0; i < big.size(); i++){
        CHECK(big[i].sample == tiny[i].sample);
        CHECK(big[i].pitch == tiny[i].pitch);
    }
}

TEST_CASE("a long run of low notes: every pluck found on time, nothing extra"){
    // Low notes ring for 0.6 s each: long, slow waves that a too-short level measurement mistakes for new notes
    std::vector<TestNote> notes;
    for (int k = 0; k < 24; k++) notes.push_back({1.03 + k * 0.6, 52 + k % 5});
    NoteDetectorConfig config;
    config.minFrequency = 40.0f;
    checkMatches(detect(playNotes(notes, 16.0), 800, config), notes);
}
