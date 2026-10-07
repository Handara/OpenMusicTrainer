#include "doctest/doctest.h"

#include "core/backing.h"
#include "core/notedetector.h"
#include "core/polyphony.h"

#include <string>
#include <vector>

// A recording of a real guitar (a Scarlett Solo, its inputs mixed), made with the Instrument screen's "Record a check"
// on 2026-10-04: tests/data/guitar-chords.wav, open G and C chords strummed, a few times each. Before, the chord
// listener heard two notes of a strum at best, and gave up on most: a strum's strings came as several attacks.

TEST_CASE("a real guitar: strummed chords are named, and none wrongly"){
    std::vector<float> samples;
    int rate = 0;
    std::string error;
    REQUIRE_MESSAGE(readWav(std::string(LAHN_TEST_DATA_DIR) + "guitar-chords.wav", samples, rate, error), error);
    NoteDetectorConfig config;
    config.minFrequency = 74.0f; // a guitar's low E, a little flat
    NoteDetector detector;
    initNoteDetector(detector, rate, config);
    PluckListener listener;
    initPluckListener(listener, rate, 39, 39 + 48);
    std::vector<DetectedNote> notes;
    std::vector<PluckNotes> plucks;
    for (size_t at = 0; at < samples.size(); at += 735){
        notes.clear();
        const int count = (int)std::min<size_t>(735, samples.size() - at);
        feedNoteDetector(detector, samples.data() + at, count, notes);
        feedPluckListener(listener, samples.data() + at, count, detector.attacks, detector.changes, plucks);
        detector.attacks.clear();
        detector.changes.clear();
    }
    int g = 0, c = 0;
    for (const PluckNotes& pluck : plucks){
        const double seconds = pluck.sample / (double)rate;
        if (pluck.chord.empty()) continue;
        if (seconds < 4.5) CHECK_MESSAGE(pluck.chord == "G", "at " << seconds << " s: " << pluck.chord);
        else CHECK_MESSAGE(pluck.chord == "C", "at " << seconds << " s: " << pluck.chord);
        g += pluck.chord == "G";
        c += pluck.chord == "C";
    }
    CHECK(g >= 3); // of 4 strums
    CHECK(c >= 3); // of 6
}
