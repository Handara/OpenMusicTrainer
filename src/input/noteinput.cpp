#include "input/noteinput.h"

#include "audio/audio.h"
#include "core/notedetector.h"

#include <algorithm>
#include <cmath>

static struct {
    NoteDetector detector;
    std::vector<float> buffer;             // scratch for reading the capture buffer
    std::vector<DetectedNote> detected;    // reused every frame: no allocation once warmed up
    std::vector<PlayedNote> played;
    std::vector<float> latest;             // everything read by the last update
    float levelDb = -100.0f;
    bool active = false;
} input;

bool startNoteInput(const std::string& inputDevice, float minFrequency, std::string& error){
    stopNoteInput();
    if (!startCapture(inputDevice, error)) return false;
    NoteDetectorConfig config;
    config.minFrequency = minFrequency;
    initNoteDetector(input.detector, captureSampleRate(), config);
    input.buffer.assign(4096, 0.0f);
    input.levelDb = -100.0f;
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
    input.latest.clear();
    if (!input.active) return input.played;

    float sumSquares = 0.0f;
    int total = 0;
    int got;
    while ((got = readCapture(input.buffer.data(), (int)input.buffer.size())) > 0){
        feedNoteDetector(input.detector, input.buffer.data(), got, input.detected);
        input.latest.insert(input.latest.end(), input.buffer.begin(), input.buffer.begin() + got);
        for (int i = 0; i < got; i++) sumSquares += input.buffer[i] * input.buffer[i];
        total += got;
    }
    if (total > 0) input.levelDb = 20.0f * std::log10(std::max(std::sqrt(sumSquares / total), 1e-6f));

    // How long ago each note started, counted back from the newest sample the detector has seen
    for (const DetectedNote& note : input.detected){
        double age = (double)(input.detector.position - note.sample) / input.detector.sampleRate;
        input.played.push_back({note.pitch, note.cents, age});
    }
    return input.played;
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
