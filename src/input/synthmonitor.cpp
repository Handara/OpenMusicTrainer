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
    int expected = -1;     // the note a song has due now (expectSynthNote), for this frame
    int startedEarly = -1; // a note started at its attack, from the song, not yet confirmed by its pitch
} synth;

void expectSynthNote(int pitch){
    synth.expected = pitch;
}

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
    // A pluck, with a song saying which note is due: it plays at once, at the attack
    if (!synth.detector.attacks.empty() && synth.expected >= 0){
        playSynthNote(midiToFrequency((float)synth.expected), volume);
        synth.playing = true;
        synth.startedEarly = synth.expected;
    }
    synth.detector.attacks.clear();
    // Each note found: played, unless it's the one already started at its attack
    for (const DetectedNote& note : synth.notes){
        bool alreadyPlaying = note.pitch == synth.startedEarly;
        synth.startedEarly = -1;
        if (alreadyPlaying) continue;
        playSynthNote(midiToFrequency((float)note.pitch), volume);
        synth.playing = true;
    }
    synth.expected = -1; // good for this frame only
    // The string muted (the sound died away, with no new note on its way): the synth note fades too
    if (synth.playing && !synth.detector.sounding && !synth.detector.pitchPending && synth.startedEarly < 0){
        releaseSynthNote();
        synth.playing = false;
    }
}
