#include "doctest/doctest.h"

#include "core/music.h"
#include "core/notedetector.h"
#include "core/polyphony.h"
#include "core/synth.h"

#include <string>
#include <vector>

// Notes plucked together on lahn's own string voices, each `gains[i]` loud and `cents[i]` out of tune, half a second
static std::vector<float> pluck(const std::vector<int>& pitches, int rate, StringVoice voice, const std::vector<float>& gains = {},
                                const std::vector<float>& cents = {}){
    const int count = rate / 2;
    std::vector<float> out(count, 0.0f), note(count);
    for (size_t i = 0; i < pitches.size(); i++){
        float midi = pitches[i] + (i < cents.size() ? cents[i] / 100.0f : 0.0f);
        renderStringNote(note.data(), count, midiToFrequency(midi), rate, voice, 7 + (unsigned)i);
        float gain = 0.5f * (i < gains.size() ? gains[i] : 1.0f);
        for (int s = 0; s < count; s++) out[s] += gain * note[s];
    }
    return out;
}

static std::vector<int> heardIn(const std::vector<float>& sound, int rate, int lowest, int highest){
    std::vector<int> pitches;
    for (const HeardPitch& note : notesInSound(sound.data(), (int)(NOTES_LISTEN_S * rate), rate, lowest, highest)) pitches.push_back(note.pitch);
    return pitches;
}

TEST_CASE("one note is heard as one note: never its octave or its fifth with it"){
    for (int rate : { 44100, 48000 }){
        for (int pitch = 28; pitch <= 60; pitch++){ // a bass, its low E to past its 17th fret on the G
            CAPTURE(rate); CAPTURE(pitch);
            CHECK(heardIn(pluck({ pitch }, rate, StringVoice::Bass), rate, 28, 67) == std::vector<int>{ pitch });
        }
        for (int pitch = 40; pitch <= 84; pitch++){ // a guitar
            CAPTURE(rate); CAPTURE(pitch);
            CHECK(heardIn(pluck({ pitch }, rate, StringVoice::Guitar), rate, 40, 88) == std::vector<int>{ pitch });
        }
    }
}

TEST_CASE("two notes plucked together are both heard: a bass's double stops"){
    const int rate = 48000;
    // Fourths, fifths, sixths and tenths, all along the neck; thirds from the A string up (lower, they're a growl)
    for (int interval : { 4, 5, 7, 9, 16 }){
        for (int low = interval == 4 ? 33 : 28; low + interval <= 62 && low <= 50; low++){
            CAPTURE(interval); CAPTURE(low);
            CHECK(heardIn(pluck({ low, low + interval }, rate, StringVoice::Bass), rate, 28, 67) == std::vector<int>{ low, low + interval });
        }
    }
    // One string plucked harder than the other, and a little out of tune with it
    CHECK(heardIn(pluck({ 40, 47 }, rate, StringVoice::Bass, { 1.0f, 0.6f }, { 0.0f, 12.0f }), rate, 28, 67) == std::vector<int>{ 40, 47 });
    CHECK(heardIn(pluck({ 40, 47 }, rate, StringVoice::Bass, { 0.6f, 1.0f }, { -10.0f, 8.0f }), rate, 28, 67) == std::vector<int>{ 40, 47 });
    CHECK(heardIn(pluck({ 33, 38 }, 44100, StringVoice::Bass, { 1.0f, 0.7f }), 44100, 28, 67) == std::vector<int>{ 33, 38 });
}

TEST_CASE("a guitar's double stops, and three notes together"){
    const int rate = 44100;
    for (int interval : { 3, 4, 5, 7, 9 }){
        for (int low = 40; low <= 64; low += 3){
            CAPTURE(interval); CAPTURE(low);
            CHECK(heardIn(pluck({ low, low + interval }, rate, StringVoice::Guitar), rate, 40, 88) == std::vector<int>{ low, low + interval });
        }
    }
    CHECK(heardIn(pluck({ 48, 52, 55 }, rate, StringVoice::Guitar), rate, 40, 88) == std::vector<int>{ 48, 52, 55 }); // C E G
    CHECK(heardIn(pluck({ 52, 55, 59 }, rate, StringVoice::Guitar), rate, 40, 88) == std::vector<int>{ 52, 55, 59 }); // E G B
}

TEST_CASE("an octave: the lower note for sure, the upper when it shows, and nothing that wasn't played"){
    const int rate = 48000;
    int both = 0, pairs = 0;
    for (int low = 28; low <= 48; low++){
        std::vector<int> heard = heardIn(pluck({ low, low + 12 }, rate, StringVoice::Bass), rate, 28, 67);
        CAPTURE(low);
        REQUIRE(!heard.empty());
        CHECK(heard[0] == low);
        if (heard.size() > 1) CHECK(heard == std::vector<int>{ low, low + 12 });
        both += heard.size() == 2;
        pairs++;
    }
    CAPTURE(both);
    CHECK(both >= pairs / 3); // every harmonic of the upper note sits on one of the lower's: it's often not told
}

TEST_CASE("silence and noise hold no notes"){
    std::vector<float> silence(9600, 0.0f), hiss(9600);
    unsigned seed = 5;
    for (float& sample : hiss){ seed = seed * 1664525u + 1013904223u; sample = 0.0005f * ((seed >> 8) / 8388608.0f - 1.0f); }
    CHECK(notesInSound(silence.data(), 9600, 48000, 28, 67).empty());
    CHECK(notesInSound(hiss.data(), 9600, 48000, 28, 67).empty());
    CHECK(notesInSound(silence.data(), 10, 48000, 28, 67).empty());
}

// A stream as the game hears it: sounds laid on a silence at given times, fed frame by frame to a note detector and
// the pluck listener together
struct Laid { double at; std::vector<int> pitches; double lasts; };
static std::vector<PluckNotes> listenTo(const std::vector<Laid>& sounds, int rate, double length){
    std::vector<float> stream((size_t)(length * rate), 0.0f);
    for (const Laid& sound : sounds){
        std::vector<float> plucked = pluck(sound.pitches, rate, StringVoice::Bass);
        size_t from = (size_t)(sound.at * rate), count = std::min(plucked.size(), (size_t)(sound.lasts * rate));
        for (size_t i = 0; i < count && from + i < stream.size(); i++){
            float fade = std::min(1.0f, (float)(count - i) / (0.01f * rate)); // muted, not cut
            stream[from + i] += plucked[i] * fade;
        }
    }
    NoteDetectorConfig config;
    config.minFrequency = 37.0f;
    NoteDetector detector;
    initNoteDetector(detector, rate, config);
    PluckListener listener;
    initPluckListener(listener, rate, 28, 67);
    std::vector<DetectedNote> notes;
    std::vector<PluckNotes> found;
    const size_t frame = rate / 60;
    for (size_t at = 0; at < stream.size(); at += frame){
        int count = (int)std::min(frame, stream.size() - at);
        notes.clear();
        feedNoteDetector(detector, stream.data() + at, count, notes);
        feedPluckListener(listener, stream.data() + at, count, detector.attacks, notes, found);
        detector.attacks.clear();
    }
    return found;
}

TEST_CASE("in a stream: a double stop is found at its pluck, single notes aren't"){
    const int rate = 48000;
    std::vector<PluckNotes> found = listenTo({ { 0.30, { 40 }, 0.4 }, { 0.90, { 40, 47 }, 0.5 }, { 1.60, { 45 }, 0.4 }, { 2.20, { 33, 38 }, 0.5 } }, rate, 3.0);
    REQUIRE(found.size() == 2);
    CHECK(found[0].pitches == std::vector<int>{ 40, 47 });
    CHECK(found[0].sample / (double)rate == doctest::Approx(0.90).epsilon(0.02));
    CHECK(found[1].pitches == std::vector<int>{ 33, 38 });
    CHECK(found[1].sample / (double)rate == doctest::Approx(2.20).epsilon(0.02));
}

TEST_CASE("in a stream: a note still ringing when another is plucked isn't played again"){
    // The A rings on under the E plucked half a second later: one note each time
    CHECK(listenTo({ { 0.30, { 33 }, 0.5 }, { 0.80, { 40 }, 0.5 } }, 48000, 1.6).empty());
    // But plucked again with it, it counts
    std::vector<PluckNotes> found = listenTo({ { 0.30, { 33 }, 0.5 }, { 0.80, { 33, 40 }, 0.5 } }, 48000, 1.6);
    REQUIRE(found.size() == 1);
    CHECK(found[0].pitches == std::vector<int>{ 33, 40 });
}

TEST_CASE("in a stream: notes one after the other, fast, aren't notes together"){
    // Sixteenths at 150 beats a minute: each note muted as the next starts
    std::vector<Laid> run;
    const int line[] = { 40, 43, 45, 47, 45, 43, 40, 38 };
    for (int i = 0; i < 8; i++) run.push_back({ 0.3 + 0.1 * i, { line[i] }, 0.1 });
    CHECK(listenTo(run, 48000, 1.6).empty());
}
