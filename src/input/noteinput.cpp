#include "input/noteinput.h"

#include "audio/audio.h"
#include "core/backing.h"
#include "core/music.h"
#include "core/notedetector.h"
#include "core/polyphony.h"
#include "raylib.h"

#include <algorithm>
#include <cmath>
#include <fstream>

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
    float minFrequency = 0.0f;
    bool active = false;
    // A check being recorded (startInputRecording)
    bool recording = false;
    long long recordFrom = 0;              // the detector's position when it began
    std::vector<float> recorded;
    std::vector<std::string> found;        // one line each: what was found, and when
} input;

const double RECORD_MAX_S = 300.0;

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
    input.minFrequency = minFrequency;
    input.active = true;
    return true;
}

void stopNoteInput(){
    if (!input.active) return;
    input.recording = false;
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

    feedPluckListener(input.listener, input.latest.data(), (int)input.latest.size(), input.detector.attacks, input.detector.changes, input.plucked);
    input.detector.changes.clear();
    for (const PluckNotes& pluck : input.plucked){
        input.chords.push_back({ pluck.pitches, pluck.chord, (double)(input.detector.position - pluck.sample) / input.detector.sampleRate });
    }

    if (input.recording){
        const int rate = input.detector.sampleRate;
        auto seconds = [&](long long sample){ return (double)(sample - input.recordFrom) / rate; };
        for (long long attack : input.detector.attacks) input.found.push_back(TextFormat("%9.3f  attack", seconds(attack)));
        for (const DetectedNote& note : input.detected)
            input.found.push_back(TextFormat("%9.3f  note   %s%d %+.0f cents", seconds(note.sample), pitchClassName(note.pitch), pitchOctave(note.pitch), note.cents));
        for (const PluckNotes& pluck : input.plucked){
            std::string names;
            for (int pitch : pluck.pitches) names += TextFormat(" %s%d", pitchClassName(pitch), pitchOctave(pitch));
            input.found.push_back(TextFormat("%9.3f  pluck of several:%s%s%s", seconds(pluck.sample), names.c_str(),
                                             pluck.chord.empty() ? "" : "  chord ", pluck.chord.c_str()));
        }
        if (input.recorded.size() < (size_t)(RECORD_MAX_S * rate)) input.recorded.insert(input.recorded.end(), input.latest.begin(), input.latest.end());
    }

    // How long ago each attack and note started, counted back from the newest sample the detector has seen
    for (long long attack : input.detector.attacks) input.attacks.push_back((double)(input.detector.position - attack) / input.detector.sampleRate);
    input.detector.attacks.clear();
    for (const DetectedNote& note : input.detected){
        double age = (double)(input.detector.position - note.sample) / input.detector.sampleRate;
        input.played.push_back({note.pitch, note.cents, age, note.legato, note.technique});
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

double noteInputPendingAge(){
    const long long sample = input.active ? noteDetectorPending(input.detector) : -1;
    return sample < 0 ? -1.0 : (double)(input.detector.position - sample) / input.detector.sampleRate;
}

bool noteInputHeldChange(int& pitch, double& age){
    long long sample;
    if (!input.active || !noteDetectorHeldChange(input.detector, sample, pitch)) return false;
    age = (double)(input.detector.position - sample) / input.detector.sampleRate;
    return true;
}

float noteInputLivePitch(){
    return input.active && input.detector.sounding ? input.detector.liveMidi : -1.0f;
}

int noteInputSampleRate(){
    return input.active ? input.detector.sampleRate : 0;
}

float noteInputLevelDb(){
    return input.levelDb;
}

void startInputRecording(){
    if (!input.active) return;
    input.recording = true;
    input.recordFrom = input.detector.position;
    input.recorded.clear();
    input.found.clear();
}

bool inputRecording(){
    return input.recording;
}

double inputRecordingSeconds(){
    return input.recording && input.detector.sampleRate > 0 ? (double)input.recorded.size() / input.detector.sampleRate : 0.0;
}

void cancelInputRecording(){
    input.recording = false;
    input.recorded.clear();
    input.found.clear();
}

bool saveInputRecording(const std::string& basePath, std::string& error){
    input.recording = false;
    if (!writeWav(basePath + ".wav", input.recorded, input.detector.sampleRate, error)) return false;
    std::ofstream out(basePath + ".txt");
    if (!out){
        error = "could not write " + basePath + ".txt";
        return false;
    }
    out << "# what lahn's note detector found in " << basePath << ".wav\n";
    out << "# " << input.detector.sampleRate << " Hz, input " << (input.channel < 0 ? std::string("all mixed") : std::to_string(input.channel + 1))
        << ", notes looked for down to " << input.minFrequency << " Hz\n";
    out << "#  seconds  what\n";
    for (const std::string& line : input.found) out << line << "\n";
    input.recorded.clear();
    input.found.clear();
    return true;
}
