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

TEST_CASE("a note between two pitches, drifting a little as it rings, is one note"){
    // Measured on a real bass: its low E a quarter-tone flat (E1 -49 cents), wobbling a few cents around the line
    // between E1 and D#1. Rounded to the nearest note, it flipped between the two, and each flip was taken for a
    // slide to a new note: twelve notes from one.
    std::vector<float> signal((size_t)(3.0 * RATE), 0.0f);
    double phase = 0.0;
    for (size_t i = (size_t)(0.2 * RATE); i < signal.size(); i++){
        double t = (double)i / RATE - 0.2;
        double midi = 27.51 + 0.08 * std::sin(2 * 3.14159265 * 0.7 * t); // 49 cents under E1, +-8 cents
        phase += midiToFrequency((float)midi) / RATE;
        double wave = std::sin(2 * 3.14159265 * phase) + 0.5 * std::sin(4 * 3.14159265 * phase) + 0.3 * std::sin(6 * 3.14159265 * phase);
        signal[i] = (float)(0.4 * std::exp(-t / 1.5) * wave);
    }
    NoteDetectorConfig bass;
    bass.minFrequency = 37.0f;
    std::vector<DetectedNote> found = detect(signal, 480, bass);
    REQUIRE(found.size() == 1);
    CHECK((found[0].pitch == 27 || found[0].pitch == 28));
}

TEST_CASE("a ringing note whose level wobbles (beating) is still one note"){
    // Measured on a real bass: after one pluck, its low E rang down with its level dipping 5 to 7 dB and coming
    // back every tenth of a second. Each comeback was a 6 dB rise, and was taken for a new pluck.
    std::vector<float> signal((size_t)(2.0 * RATE), 0.0f);
    for (size_t i = (size_t)(0.2 * RATE); i < signal.size(); i++){
        double t = (double)i / RATE - 0.2;
        double phase = midiToFrequency(28.0f) * t;
        double wave = std::sin(2 * 3.14159265 * phase) + 0.5 * std::sin(4 * 3.14159265 * phase);
        double wobble = std::pow(10.0, -7.0 / 20.0 * (0.5 - 0.5 * std::cos(2 * 3.14159265 * 10.0 * t))); // 0 to -7 dB
        signal[i] = (float)(0.4 * std::exp(-t / 1.0) * wobble * wave);
    }
    NoteDetectorConfig bass;
    bass.minFrequency = 37.0f;
    std::vector<DetectedNote> found = detect(signal, 480, bass);
    CHECK(found.size() == 1);
}

TEST_CASE("a low note whose overtones outlast it doesn't turn into them"){
    // Measured on a real bass: a low E's fundamental died away faster than its overtones, and near the end the
    // pitch read as B2, its third harmonic (an octave and a fifth up), which was taken for a slide to a new note
    std::vector<float> signal((size_t)(3.0 * RATE), 0.0f);
    for (size_t i = (size_t)(0.2 * RATE); i < signal.size(); i++){
        double t = (double)i / RATE - 0.2;
        double phase = midiToFrequency(28.0f) * t;
        double fundamental = std::exp(-t / 0.25) * std::sin(2 * 3.14159265 * phase);
        double overtones = 0.3 * std::exp(-t / 3.0) * std::sin(6 * 3.14159265 * phase); // the third harmonic lingers
        signal[i] = (float)(0.5 * (fundamental + overtones));
    }
    NoteDetectorConfig bass;
    bass.minFrequency = 37.0f;
    std::vector<DetectedNote> found = detect(signal, 480, bass);
    REQUIRE(found.size() == 1);
    CHECK(found[0].pitch == 28);
}

// How long after the start of the signal a note was reported, fed a millisecond at a time as a device would
static double reportedAfter(const std::vector<float>& signal, NoteDetector& detector, std::vector<DetectedNote>& found){
    for (size_t i = 0; i < signal.size(); i += 48){
        feedNoteDetector(detector, signal.data() + i, (int)std::min<size_t>(48, signal.size() - i), found);
        if (!found.empty()) return (double)(i + 48) / RATE;
    }
    return -1.0;
}

TEST_CASE("told the lowest note that can come, a bass's detector names a higher note far sooner"){
    // A bass detector must be ready for E1, whose pitch takes two of its long periods (54 ms) to be sure of. When the
    // song says nothing below E3 is due, two of E3's periods do: the note is known within ~25 ms of the pluck.
    std::vector<TestNote> notes = {{0.2, 52}};
    std::vector<float> signal = playNotes(notes, 0.6);
    NoteDetectorConfig bass;
    bass.minFrequency = 37.0f;

    NoteDetector unhinted;
    initNoteDetector(unhinted, RATE, bass);
    std::vector<DetectedNote> slow;
    double slowAfter = reportedAfter(signal, unhinted, slow) - 0.2;

    NoteDetector hinted;
    initNoteDetector(hinted, RATE, bass);
    expectLowestFrequency(hinted, midiToFrequency(52.0f));
    std::vector<DetectedNote> fast;
    double fastAfter = reportedAfter(signal, hinted, fast) - 0.2;

    REQUIRE(slow.size() == 1);
    REQUIRE(fast.size() == 1);
    CHECK(fast[0].pitch == 52);
    CHECK(std::fabs(fast[0].sample / (double)RATE - 0.2) < 0.001); // stamped at the pluck all the same
    CHECK(slowAfter > 0.060);
    CHECK(fastAfter < 0.030);
}

TEST_CASE("a hinted detector still reads a melody right, and goes back to any note when told nothing is due"){
    std::vector<TestNote> notes = {{0.10, 52}, {0.35, 55}, {0.60, 57}, {0.85, 59}};
    NoteDetectorConfig bass;
    bass.minFrequency = 37.0f;
    NoteDetector detector;
    initNoteDetector(detector, RATE, bass);
    expectLowestFrequency(detector, midiToFrequency(52.0f));
    std::vector<float> signal = playNotes(notes, 1.2);
    std::vector<DetectedNote> found;
    feedNoteDetector(detector, signal.data(), (int)signal.size(), found);
    checkMatches(found, notes);

    // No hint: the low E of a bass is found again
    expectLowestFrequency(detector, 0.0f);
    std::vector<TestNote> low = {{0.2, 28}};
    std::vector<float> lowSignal = playNotes(low, 0.8);
    std::vector<DetectedNote> lowFound;
    feedNoteDetector(detector, lowSignal.data(), (int)lowSignal.size(), lowFound);
    REQUIRE(lowFound.size() == 1);
    CHECK(lowFound[0].pitch == 28);
}

TEST_CASE("an attack is reported the moment it's heard, before its pitch"){
    std::vector<TestNote> notes = {{0.2, 28}, {0.7, 33}}; // a bass's E1 and A1: their pitches take ~54 ms
    std::vector<float> signal = playNotes(notes, 1.2);
    NoteDetectorConfig bass;
    bass.minFrequency = 37.0f;
    NoteDetector detector;
    initNoteDetector(detector, RATE, bass);
    std::vector<DetectedNote> found;
    std::vector<long long> heardAt; // how far the stream was when each attack was reported
    for (size_t i = 0; i < signal.size(); i += 48){
        feedNoteDetector(detector, signal.data() + i, (int)std::min<size_t>(48, signal.size() - i), found);
        for (long long attack : detector.attacks){
            heardAt.push_back(detector.position);
            CHECK(std::fabs(attack / (double)RATE - (heardAt.size() == 1 ? 0.2 : 0.7)) < 0.001); // placed at the pluck
        }
        detector.attacks.clear();
    }
    REQUIRE(heardAt.size() == 2);
    CHECK(heardAt[0] / (double)RATE - 0.2 < 0.005); // within a couple of ms, where the pitch took ~65
    CHECK(heardAt[1] / (double)RATE - 0.7 < 0.005);
    CHECK(found.size() == 2);
}
