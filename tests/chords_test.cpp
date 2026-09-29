#include "doctest/doctest.h"

#include "core/chords.h"
#include "core/exercisefile.h"
#include "core/synth.h"
#include "core/music.h"

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
    std::filesystem::path path = std::filesystem::temp_directory_path() / "lahn_tests" / "exercises" / "changes.exercise";
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
