// Makes the sample song, "First Light": its chart and its audio, from the same notes, with the game's own synth.
// The chart and the audio can't disagree, and the song can be changed here and made again:
//     cmake --build build --target make_song && ./build/make_song resources/songs/first-light
//
// 84 bpm in E minor: a bar of electric piano to feel the beat, then eight bars of a plucked guitar melody over
// Em C G D Em C Am B7, with the piano and a soft bass underneath, and a last E held for a bar. Two parts to play:
// the melody on guitar, and the bass line (each chord's root on beats 1 and 3) on a 4-string bass.

#include "core/chart.h"
#include "core/music.h"
#include "core/synth.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;

const int SAMPLE_RATE = 44100;
const int RESOLUTION = 480;              // ticks per quarter note
const double BPM = 84.0;
const int BAR = RESOLUTION * 4;
const int INTRO_BARS = 1;                // piano alone first
const int BARS = INTRO_BARS + 8 + 1;     // then eight bars of melody and the last bar's held E
const std::vector<int> TUNING = { 40, 45, 50, 55, 59, 64 }; // standard guitar, low to high
const std::vector<int> BASS_TUNING = { 28, 33, 38, 43 };    // standard 4-string bass

// A melody note: when (in eighths from its bar's start), how long (in eighths), and the pitch (MIDI)
struct MelodyNote { int eighth; int length; int pitch; };

// The eight melody bars, one per chord: arpeggios through each chord, quarters and eighths, a dotted quarter
// and half notes, so the sheet music shows real rhythm. The B7 bar brings a D#, outside the key.
const std::vector<std::vector<MelodyNote>> MELODY = {
    {{0, 2, 52}, {2, 1, 55}, {3, 1, 59}, {4, 2, 64}, {6, 2, 59}},       // Em
    {{0, 2, 48}, {2, 1, 52}, {3, 1, 55}, {4, 4, 60}},                   // C
    {{0, 1, 55}, {1, 1, 59}, {2, 1, 62}, {3, 1, 59}, {4, 2, 55}, {6, 2, 50}}, // G
    {{0, 2, 50}, {2, 1, 54}, {3, 1, 57}, {4, 4, 62}},                   // D
    {{0, 3, 64}, {3, 1, 62}, {4, 2, 59}, {6, 2, 55}},                   // Em
    {{0, 2, 60}, {2, 2, 64}, {4, 2, 67}, {6, 2, 64}},                   // C
    {{0, 1, 57}, {1, 1, 60}, {2, 1, 64}, {3, 1, 60}, {4, 2, 57}, {6, 2, 52}}, // Am
    {{0, 2, 59}, {2, 2, 63}, {4, 2, 66}, {6, 2, 63}},                   // B7
};

// Each bar's chord for the piano (close voicing, middle register) and the bass (its root, low). The intro and the
// ending play Em.
struct Chord { std::vector<int> piano; int bass; };
const Chord EM = {{52, 55, 59}, 40}, C = {{48, 52, 55}, 36}, G = {{50, 55, 59}, 43}, D = {{50, 54, 57}, 38},
            AM = {{48, 52, 57}, 45}, B7 = {{51, 54, 57, 59}, 47};
const std::vector<Chord> CHORDS = { EM, EM, C, G, D, EM, C, AM, B7, EM };

// Where a pitch is played: the highest string that has it in the first four frets (open position)
static bool place(const std::vector<int>& tuning, int pitch, int& stringIndex, int& fret){
    for (int s = (int)tuning.size() - 1; s >= 0; s--){
        int f = pitch - tuning[s];
        if (f >= 0 && f <= 4){ stringIndex = s; fret = f; return true; }
    }
    return false;
}

static double secondsAt(int tick){
    return (double)tick / RESOLUTION * 60.0 / BPM;
}

// Adds a rendered sound into the mix at a time, scaled
static void mix(std::vector<float>& song, const std::vector<float>& sound, double time, float gain){
    size_t start = (size_t)(time * SAMPLE_RATE);
    for (size_t i = 0; i < sound.size() && start + i < song.size(); i++) song[start + i] += sound[i] * gain;
}

// 16-bit mono PCM: the plainest WAV there is
static bool writeWav(const std::string& path, const std::vector<float>& samples){
    FILE* file = std::fopen(path.c_str(), "wb");
    if (!file) return false;
    auto u32 = [&](uint32_t v){ std::fwrite(&v, 4, 1, file); };
    auto u16 = [&](uint16_t v){ std::fwrite(&v, 2, 1, file); };
    uint32_t dataSize = (uint32_t)samples.size() * 2;
    std::fwrite("RIFF", 1, 4, file); u32(36 + dataSize); std::fwrite("WAVE", 1, 4, file);
    std::fwrite("fmt ", 1, 4, file); u32(16); u16(1); u16(1); u32(SAMPLE_RATE); u32(SAMPLE_RATE * 2); u16(2); u16(16);
    std::fwrite("data", 1, 4, file); u32(dataSize);
    for (float sample : samples){
        int16_t value = (int16_t)std::lround(std::clamp(sample, -1.0f, 1.0f) * 32767.0f);
        std::fwrite(&value, 2, 1, file);
    }
    return std::fclose(file) == 0;
}

int main(int argc, char** argv){
    if (argc != 2){
        std::printf("usage: make_song <song folder>\n");
        return 1;
    }
    fs::path folder = argv[1];
    fs::create_directories(folder);

    // The chart: the melody on the guitar track
    Chart chart{};
    chart.version = 2;
    chart.title = "First Light";
    chart.artist = "lahn";
    chart.audioFile = "audio.wav";
    chart.resolution = RESOLUTION;
    chart.offset = 0.0;
    chart.endTick = BARS * BAR;
    chart.tempoMap = {{0, BPM}};
    chart.timeSignatures = {{0, 4, 4}};
    KeySignature eMinor;
    parseKeySignature("E", "minor", eMinor);
    chart.keys = {{0, eMinor}};
    FrettedTrack guitar;
    guitar.type = InstrumentType::Guitar;
    guitar.name = "Melody";
    guitar.tuning = TUNING;
    for (size_t bar = 0; bar < MELODY.size(); bar++){
        for (const MelodyNote& note : MELODY[bar]){
            FrettedNote chartNote{};
            chartNote.tick = (int)(INTRO_BARS + bar) * BAR + note.eighth * RESOLUTION / 2;
            if (!place(TUNING, note.pitch, chartNote.stringIndex, chartNote.fret)){
                std::printf("pitch %d has no place in open position\n", note.pitch);
                return 1;
            }
            guitar.notes.push_back(chartNote);
        }
    }
    FrettedNote last{};                       // the last E, held for the whole last bar
    last.tick = (BARS - 1) * BAR;
    last.duration = BAR;
    place(TUNING, 64, last.stringIndex, last.fret);
    guitar.notes.push_back(last);

    // The bass part: what the bass plays in the audio below, the chord's root on beats 1 and 3 of every bar
    FrettedTrack bass;
    bass.type = InstrumentType::Bass;
    bass.name = "Bass";
    bass.tuning = BASS_TUNING;
    for (int bar = 0; bar < BARS; bar++){
        for (int beat : {0, 2}){
            FrettedNote chartNote{};
            chartNote.tick = bar * BAR + beat * RESOLUTION;
            if (!place(BASS_TUNING, CHORDS[bar].bass, chartNote.stringIndex, chartNote.fret)){
                std::printf("bass pitch %d has no place in open position\n", CHORDS[bar].bass);
                return 1;
            }
            bass.notes.push_back(chartNote);
        }
    }
    chart.frettedTracks = {guitar, bass};

    // The audio: the same notes plucked, over the piano and the bass
    double length = secondsAt(chart.endTick) + 2.5; // the last notes ring out
    std::vector<float> song((size_t)(length * SAMPLE_RATE), 0.0f);
    unsigned seed = 1;
    for (const FrettedNote& note : guitar.notes){
        int pitch = TUNING[note.stringIndex] + note.fret;
        std::vector<float> sound((size_t)(2.4 * SAMPLE_RATE));
        renderPluck(sound.data(), (int)sound.size(), midiToFrequency((float)pitch), SAMPLE_RATE, seed++);
        mix(song, sound, secondsAt(note.tick), 0.55f);
    }
    for (int bar = 0; bar < BARS; bar++){
        const Chord& chord = CHORDS[bar];
        for (int beat : {0, 2}){ // the chord on beats 1 and 3, the second one softer
            double time = secondsAt(bar * BAR + beat * RESOLUTION);
            for (int pitch : chord.piano){
                std::vector<float> sound((size_t)(1.6 * SAMPLE_RATE));
                renderKeys(sound.data(), (int)sound.size(), midiToFrequency((float)pitch), SAMPLE_RATE);
                mix(song, sound, time, beat == 0 ? 0.13f : 0.09f);
            }
            std::vector<float> bass((size_t)(1.3 * SAMPLE_RATE));
            renderSoftTone(bass.data(), (int)bass.size(), midiToFrequency((float)chord.bass), SAMPLE_RATE);
            mix(song, bass, time, 0.32f);
        }
    }

    // Loud enough, never clipping: the peak brought to 0.9
    float peak = 0.0f;
    for (float sample : song) peak = std::max(peak, std::fabs(sample));
    if (peak > 0.0f) for (float& sample : song) sample *= 0.9f / peak;

    std::string error;
    if (!saveChart((folder / "song.chart").string(), chart, error)){
        std::printf("%s\n", error.c_str());
        return 1;
    }
    if (!writeWav((folder / "audio.wav").string(), song)){
        std::printf("could not write %s\n", (folder / "audio.wav").string().c_str());
        return 1;
    }
    std::printf("wrote %s: %d melody notes, %d bass notes, %.1f s\n", folder.string().c_str(), (int)guitar.notes.size(),
                (int)bass.notes.size(), length);
    return 0;
}
