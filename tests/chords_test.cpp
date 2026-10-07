#include "doctest/doctest.h"

#include "core/chords.h"
#include "core/exercisefile.h"
#include "core/synth.h"
#include "core/music.h"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <vector>

const int RATE = 44100;
const int STANDARD[6] = { 40, 45, 50, 55, 59, 64 };

// A chord strummed on the pluck synth, the way a guitar would: its shape's strings, a few milliseconds apart,
// then the part of the sound a player's strum would be checked on (from 30 ms in, about 170 ms of it)
static std::vector<float> strum(const ChordInfo& chord, unsigned seed){
    std::vector<float> mix(RATE, 0.0f), note(RATE);
    int offset = 0;
    for (int s = 0; s < 6; s++){
        if (chord.shape[s] < 0) continue;
        renderPluck(note.data(), (int)note.size(), midiToFrequency((float)(STANDARD[s] + chord.shape[s])), RATE, seed + s);
        for (size_t i = 0; i + offset < mix.size(); i++) mix[i + offset] += note[i] * 0.3f;
        offset += RATE / 200; // 5 ms a string
    }
    int start = RATE * 30 / 1000;
    return std::vector<float>(mix.begin() + start, mix.begin() + start + 7500);
}

TEST_CASE("every common chord's shape plays its own notes"){
    for (const ChordInfo& chord : commonChords()){
        for (int s = 0; s < 6; s++){
            if (chord.shape[s] < 0) continue;
            int pitchClass = (STANDARD[s] + chord.shape[s]) % 12;
            bool inChord = false;
            for (int interval : chord.intervals) inChord = inChord || (chord.rootPitchClass + interval) % 12 == pitchClass;
            CHECK_MESSAGE(inChord, chord.name << ": string " << s + 1 << " plays a note outside it");
        }
    }
    CHECK(findChord("Am") != nullptr);
    CHECK(findChord("Hm") == nullptr);
}

TEST_CASE("a strummed chord is heard as itself"){
    for (const char* name : { "C", "D", "E", "G", "A", "Em", "Am", "Dm", "F" }){
        const ChordInfo& chord = *findChord(name);
        std::array<float, 12> notes = chroma(strum(chord, 3).data(), 7500, RATE);
        CHECK_MESSAGE(soundsLikeChord(notes, chord), name << " wasn't recognized");
    }
}

TEST_CASE("a wrong chord is not"){
    // The changes a beginner mixes up, and chords that share notes
    const char* pairs[][2] = { {"C", "G"}, {"G", "C"}, {"Em", "G"}, {"Am", "C"}, {"D", "A"}, {"E", "Em"}, {"Am", "Dm"} };
    for (auto& pair : pairs){
        std::array<float, 12> notes = chroma(strum(*findChord(pair[0]), 9).data(), 7500, RATE);
        CHECK_MESSAGE(!soundsLikeChord(notes, *findChord(pair[1])), pair[0] << " was heard as " << pair[1]);
    }
}

TEST_CASE("silence is no chord"){
    std::vector<float> silence(7500, 0.0f);
    std::array<float, 12> notes = chroma(silence.data(), 7500, RATE);
    CHECK_FALSE(soundsLikeChord(notes, *findChord("C")));
}

TEST_CASE("strums are found where they start, once each"){
    // Four chords, half a second apart, each ringing on under the next
    std::vector<float> stream(RATE * 5 / 2, 0.0f), note(RATE);
    const char* names[] = { "C", "G", "Am", "F" };
    for (int k = 0; k < 4; k++){
        const ChordInfo& chord = *findChord(names[k]);
        int start = RATE / 4 + k * RATE / 2;
        for (int s = 0, offset = 0; s < 6; s++){
            if (chord.shape[s] < 0) continue;
            renderPluck(note.data(), (int)note.size(), midiToFrequency((float)(STANDARD[s] + chord.shape[s])), RATE, 20 + k * 6 + s);
            for (size_t i = 0; i < note.size() && start + offset + i < stream.size(); i++) stream[start + offset + i] += note[i] * 0.3f;
            offset += RATE / 200;
        }
    }
    StrumDetector detector;
    initStrumDetector(detector, RATE);
    std::vector<long long> strums;
    for (size_t at = 0; at < stream.size(); at += 512){ // fed in pieces, as the input delivers it
        feedStrumDetector(detector, stream.data() + at, (int)std::min<size_t>(512, stream.size() - at), strums);
    }
    REQUIRE(strums.size() == 4);
    for (int k = 0; k < 4; k++){
        long long expected = RATE / 4 + k * RATE / 2;
        CHECK(std::llabs(strums[k] - expected) < RATE / 50); // within 20 ms of where it starts
    }
}

TEST_CASE("chord change exercise files"){
    std::filesystem::path path = std::filesystem::temp_directory_path() / "hardthz_tests" / "exercises" / "changes.exercise";
    std::filesystem::create_directories(path.parent_path());
    std::ofstream(path, std::ios::binary) << "version 1\ntype chords\ntitle G to C\nchords G C D\nbeats 2\nrounds 3\n";
    ExerciseFile file;
    std::string error;
    REQUIRE_MESSAGE(loadExerciseFile(path.string(), file, error), error);
    CHECK(file.type == ExerciseType::Chords);
    CHECK(file.chords.chords == std::vector<std::string>{"G", "C", "D"});
    CHECK(file.chords.beatsPerChord == 2);
    CHECK(file.chords.rounds == 3);

    std::ofstream(path, std::ios::binary) << "version 1\ntype chords\ntitle One\nchords G\n";
    CHECK_FALSE(loadExerciseFile(path.string(), file, error)); // one chord is no change
    std::ofstream(path, std::ios::binary) << "version 1\ntype chords\ntitle Odd\nchords G Hm\n";
    CHECK_FALSE(loadExerciseFile(path.string(), file, error));
    CHECK(error.find("Hm") != std::string::npos);
}

TEST_CASE("chords are named from the notes held"){
    CHECK(nameChord({60, 64, 67}) == "C");
    CHECK(nameChord({57, 60, 64}) == "Am");
    CHECK(nameChord({55, 59, 62, 65}) == "G7");
    CHECK(nameChord({48, 55, 64, 71}) == "Cmaj7");     // spread out, doubled or not, it's the same chord
    CHECK(nameChord({40, 47, 52, 55, 59, 64}) == "Em"); // the open E minor on a guitar
    CHECK(nameChord({40, 47}) == "E5");
    CHECK(nameChord({59, 62, 65}) == "Bdim");
}

TEST_CASE("an inversion is named over its bass, and the bass wins a tie"){
    CHECK(nameChord({52, 55, 60}) == "C/E");
    CHECK(nameChord({48, 52, 55, 57}) == "C6");  // C E G A over C
    CHECK(nameChord({45, 48, 52, 55}) == "Am7"); // the same notes over A
}

TEST_CASE("what isn't a chord has no name"){
    CHECK(nameChord({}) == "");
    CHECK(nameChord({60}) == "");
    CHECK(nameChord({60, 61, 62}) == "");
    CHECK(nameChord({60, 72}) == ""); // octaves: one note
}

// A plucked string, roughly: rich in harmonics, a weak fundamental (as a bass's pickups hear it), dying away
static void pluck(std::vector<float>& out, int rate, int pitch, float gain){
    float frequency = midiToFrequency((float)pitch);
    for (size_t i = 0; i < out.size(); i++){
        float t = (float)i / rate, sample = 0.0f;
        for (int h = 1; h <= 12; h++){
            float amplitude = (h == 1 ? 0.35f : 1.0f) / h;
            sample += amplitude * std::sin(2.0f * 3.14159265f * frequency * h * t + h * 0.7f);
        }
        out[i] += gain * sample * std::exp(-t * 3.0f);
    }
}

TEST_CASE("two notes plucked together are heard, low on a bass too"){
    const int rate = 48000, count = (int)(CHORD_LISTEN_S * rate);
    struct Case { std::vector<int> played, asked; bool heard; };
    const Case cases[] = {
        { {33, 40}, {33, 40}, true },  // A1 and E2: a bass's power chord
        { {28, 35}, {28, 35}, true },  // E1 and B1, as low as a bass goes
        { {38, 45}, {38, 45}, true },  // D2 and A2
        { {33, 45}, {33, 45}, true },  // an octave
        { {43, 47}, {43, 47}, true },  // a third, G2 and B2
        { {40, 47, 52}, {40, 47, 52}, true }, // a guitar's E5
        { {35, 42}, {33, 40}, false }, // a whole step off both
        { {34, 41}, {33, 40}, false }, // a half step off both
        { {33, 41}, {33, 40}, false }, // one right, one a half step off
    };
    for (const Case& c : cases){
        CAPTURE(c.played[0]); CAPTURE(c.asked[0]); CAPTURE(c.asked[1]);
        std::vector<float> sound(count, 0.0f);
        for (size_t i = 0; i < c.played.size(); i++) pluck(sound, rate, c.played[i], 0.3f / (1.0f + i * 0.3f));
        CHECK(soundHoldsNotes(sound.data(), count, rate, c.asked) == c.heard);
    }
    std::vector<float> silence(count, 0.0f);
    CHECK_FALSE(soundHoldsNotes(silence.data(), count, rate, {33, 40}));
}
