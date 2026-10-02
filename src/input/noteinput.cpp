#include "input/noteinput.h"

#include "audio/audio.h"
#include "core/music.h"
#include "core/notedetector.h"
#include "core/polyphony.h"

#include <algorithm>
#include <cmath>

static struct {
    NoteDetector detector;
    std::vector<float> buffer;             // scratch for reading the capture buffer
    std::vector<DetectedNote> detected;    // reused every frame: no allocation once warmed up
    std::vector<PlayedNote> played;
    PluckListener listener;                // hears the plucks that hold several notes
    std::vector<PluckNotes> plucked;
    std::vector<PlayedChord> chords;
    std::vector<double> attacks;           // how long ago each attack of the last update was heard
    std::vector<float> latest;             // everything read by the last update
    float levelDb = -100.0f;
    int channel = -1;                      // the device's input listened to, -1 for all mixed
    bool active = false;
} input;

bool startNoteInput(const std::string& inputDevice, float minFrequency, std::string& error, int channel){
    stopNoteInput();
    if (!startCapture(inputDevice, error)) return false;
    NoteDetectorConfig config;
    config.minFrequency = minFrequency;
    initNoteDetector(input.detector, captureSampleRate(), config);
    // From the lowest note listened for, four octaves up: a guitar's or a bass's whole neck
    int lowestPitch = (int)std::ceil(frequencyToMidi(minFrequency));
    initPluckListener(input.listener, captureSampleRate(), lowestPitch, lowestPitch + 48);
    input.buffer.assign(4096, 0.0f);
    input.levelDb = -100.0f;
    input.channel = channel;
    input.active = true;
    return true;
}

void stopNoteInput(){
    if (!input.active) return;
    stopCapture();
    input.active = false;
}

bool noteInputActive(){
    return input.active;
}

const std::vector<PlayedNote>& updateNoteInput(){
    input.detected.clear();
    input.played.clear();
    input.attacks.clear();
    input.latest.clear();
    input.plucked.clear();
    input.chords.clear();
    if (!input.active) return input.played;

    float sumSquares = 0.0f;
    int total = 0;
    int got;
    while ((got = readCapture(input.buffer.data(), (int)input.buffer.size(), input.channel)) > 0){
        feedNoteDetector(input.detector, input.buffer.data(), got, input.detected);
        input.latest.insert(input.latest.end(), input.buffer.begin(), input.buffer.begin() + got);
        for (int i = 0; i < got; i++) sumSquares += input.buffer[i] * input.buffer[i];
        total += got;
    }
    if (total > 0) input.levelDb = 20.0f * std::log10(std::max(std::sqrt(sumSquares / total), 1e-6f));

    feedPluckListener(input.listener, input.latest.data(), (int)input.latest.size(), input.detector.attacks, input.detected, input.plucked);
    for (const PluckNotes& pluck : input.plucked){
        input.chords.push_back({ pluck.pitches, (double)(input.detector.position - pluck.sample) / input.detector.sampleRate });
    }

    // How long ago each attack and note started, counted back from the newest sample the detector has seen
    for (long long attack : input.detector.attacks) input.attacks.push_back((double)(input.detector.position - attack) / input.detector.sampleRate);
    input.detector.attacks.clear();
    for (const DetectedNote& note : input.detected){
        double age = (double)(input.detector.position - note.sample) / input.detector.sampleRate;
        input.played.push_back({note.pitch, note.cents, age});
    }
    return input.played;
}

const std::vector<double>& noteInputAttacks(){
    return input.attacks;
}

const std::vector<PlayedChord>& noteInputChords(){
    return input.chords;
}

void expectLowestNote(float frequency){
    if (input.active) expectLowestFrequency(input.detector, frequency);
}

const std::vector<float>& latestInputSamples(){
    return input.latest;
}

int noteInputSampleRate(){
    return input.active ? input.detector.sampleRate : 0;
}

float noteInputLevelDb(){
    return input.levelDb;
}
