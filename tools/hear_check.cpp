// Plays a recording made with the Instrument screen's "Record a check" (a 16-bit mono WAV) through hardthz's note
// detector and pluck listener, as the game would, and prints what they found: to see why a note was misheard, and
// whether a change to the detectors hears it right.
//
//   hear_check <recording.wav> [lowest Hz] [chunk samples]
//   hear_check <recording.wav> <lowest Hz> notes <seconds> [seconds...]
//
// The second form: the notes core/polyphony finds in the sound just after each moment (up to 6, a guitar's chord)
//
// The lowest note looked for: 74 for a guitar (its low E, a little flat), 37 for a bass. Chunks: how many samples
// arrive at once (the game reads what came in since the last frame: about 735 at 44.1 kHz and 60 frames a second).

#include "core/backing.h"
#include "core/chords.h"
#include "core/music.h"
#include "core/notedetector.h"
#include "core/polyphony.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

static std::string name(int pitch){
    return std::string(pitchClassName(pitch)) + std::to_string(pitchOctave(pitch));
}

int main(int argc, char** argv){
    if (argc < 2){
        std::fprintf(stderr, "usage: hear_check <recording.wav> [lowest Hz] [chunk samples]\n");
        return 1;
    }
    std::vector<float> samples;
    int rate = 0;
    std::string error;
    if (!readWav(argv[1], samples, rate, error)){
        std::fprintf(stderr, "%s\n", error.c_str());
        return 1;
    }
    const float lowest = argc > 2 ? (float)std::atof(argv[2]) : 74.0f;
    if (argc > 4 && std::string(argv[3]) == "notes"){
        const int lowestPitch = (int)std::ceil(frequencyToMidi(lowest));
        for (int i = 4; i < argc; i++){
            const double at = std::atof(argv[i]);
            const size_t from = (size_t)(at * rate), count = (size_t)(NOTES_LISTEN_S * rate);
            if (from + count > samples.size()) continue;
            std::printf("%9.3f ", at);
            for (const HeardPitch& note : notesInSound(samples.data() + from, (int)count, rate, lowestPitch, lowestPitch + 48, 6))
                std::printf(" %s(%.2f)", name(note.pitch).c_str(), note.strength);
            const std::array<float, 12> notes = chroma(samples.data() + from, (int)count, rate);
            std::printf("   chord: %s  [", recognizeChord(notes).c_str());
            for (float share : notes) std::printf(" %.2f", share);
            std::printf(" ]\n");
        }
        return 0;
    }
    const int chunk = argc > 3 ? std::atoi(argv[3]) : 735;

    NoteDetector detector;
    NoteDetectorConfig config;
    config.minFrequency = lowest;
    initNoteDetector(detector, rate, config);
    PluckListener listener;
    const int lowestPitch = (int)std::ceil(frequencyToMidi(lowest));
    initPluckListener(listener, rate, lowestPitch, lowestPitch + 48);

    std::printf("# %s: %.1f s at %d Hz, notes looked for down to %.1f Hz\n", argv[1], samples.size() / (double)rate, rate, lowest);
    std::vector<DetectedNote> notes;
    std::vector<PluckNotes> plucks;
    for (size_t start = 0; start < samples.size(); start += chunk){
        const int count = (int)std::min<size_t>(chunk, samples.size() - start);
        notes.clear();
        plucks.clear();
        feedNoteDetector(detector, samples.data() + start, count, notes);
        feedPluckListener(listener, samples.data() + start, count, detector.attacks, detector.changes, plucks);
        for (long long attack : detector.attacks) std::printf("%9.3f  attack\n", attack / (double)rate);
        detector.attacks.clear();
        detector.changes.clear();
        for (const DetectedNote& note : notes) std::printf("%9.3f  note   %s %+.0f cents%s%s\n", note.sample / (double)rate, name(note.pitch).c_str(), note.cents,
                                                            note.legato ? "  " : "", note.legato ? techniqueName(note.technique) : "");
        for (const PluckNotes& pluck : plucks){
            std::printf("%9.3f  pluck of several:", pluck.sample / (double)rate);
            for (int pitch : pluck.pitches) std::printf(" %s", name(pitch).c_str());
            if (!pluck.chord.empty()) std::printf("  chord %s", pluck.chord.c_str());
            std::printf("\n");
        }
    }
    return 0;
}
