#include "input/synthmonitor.h"

#include "audio/audio.h"
#include "core/music.h"
#include "core/notedetector.h"

#include <vector>

const float LOWEST_HZ = 37.0f; // a bass's low E, a little flat: guitars are heard too, from higher up

static struct {
    NoteDetector detector;
    int sampleRate = 0;          // what the detector was set up for, 0 for not yet
    std::vector<float> buffer;
    std::vector<DetectedNote> notes; // reused every frame
    bool playing = false;
} synth;

void updateSynthMonitor(bool on, float volume){
    const int rate = on ? monitorSampleRate() : 0;
    if (rate == 0){
        if (synth.playing) releaseSynthNote();
        synth.playing = false;
        synth.sampleRate = 0;
        return;
    }
    if (rate != synth.sampleRate){
        NoteDetectorConfig config;
        config.minFrequency = LOWEST_HZ;
        initNoteDetector(synth.detector, rate, config);
        synth.sampleRate = rate;
        synth.buffer.assign(2048, 0.0f);
        synth.notes.reserve(16);
    }
    synth.notes.clear();
    int got;
    while ((got = readMonitor(synth.buffer.data(), (int)synth.buffer.size())) > 0){
        feedNoteDetector(synth.detector, synth.buffer.data(), got, synth.notes);
    }
    synth.detector.attacks.clear(); // not used here: a note plays once its pitch is known
    for (const DetectedNote& note : synth.notes){
        playSynthNote(midiToFrequency((float)note.pitch), volume);
        synth.playing = true;
    }
    // The string muted (the sound died away, with no new note on its way): the synth note fades too
    if (synth.playing && !synth.detector.sounding && !synth.detector.pitchPending){
        releaseSynthNote();
        synth.playing = false;
    }
}
