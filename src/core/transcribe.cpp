#include "core/transcribe.h"

#include "core/beats.h"
#include "core/notedetector.h"
#include "core/positions.h"

#include <algorithm>
#include <cmath>
#include <numeric>

const float LOWEST_HZ = 28.0f;      // under a five-string bass's low B (31 Hz)
const float HIGHEST_HZ = 500.0f;    // past a bass's 24th fret on the G (392 Hz)
const float LEVEL_PEAK = 0.8f;      // the recording is evened out to this before listening
const int LEVEL_FRAME = 512;        // samples per level reading, for where notes end
const float NOTE_DIES_DB = 24.0f;   // a note has ended when it's this far under its loudest
const double MIN_NOTE_S = 0.03;     // shorter is a slip of the detector, not a note
const int RESOLUTION = 480;
const int SIXTEENTH = RESOLUTION / 4;
const int BASS_FRETS = 24;

std::vector<HeardNote> hearNotes(const std::vector<float>& input, int sampleRate){
    std::vector<HeardNote> notes;
    if (input.empty() || sampleRate <= 0) return notes;
    // Evened out: a quiet stem and a loud recording are heard alike
    float peak = 0.0f;
    for (float sample : input) peak = std::max(peak, std::fabs(sample));
    if (peak <= 1e-6f) return notes;
    std::vector<float> samples(input.size());
    for (size_t i = 0; i < input.size(); i++) samples[i] = input[i] * LEVEL_PEAK / peak;

    // The notes' starts and pitches
    NoteDetectorConfig config;
    config.minFrequency = LOWEST_HZ;
    config.maxFrequency = HIGHEST_HZ;
    NoteDetector detector;
    initNoteDetector(detector, sampleRate, config);
    std::vector<DetectedNote> detected;
    for (size_t at = 0; at < samples.size(); at += 4096){
        feedNoteDetector(detector, samples.data() + at, (int)std::min<size_t>(4096, samples.size() - at), detected);
    }

    // How loud it is, frame by frame: a note ends where it dies away, or where the next one starts
    std::vector<float> levels(samples.size() / LEVEL_FRAME + 1, -120.0f);
    for (size_t frame = 0; frame < levels.size(); frame++){
        double squares = 0.0;
        size_t from = frame * LEVEL_FRAME, to = std::min(samples.size(), from + LEVEL_FRAME);
        for (size_t i = from; i < to; i++) squares += samples[i] * samples[i];
        if (to > from) levels[frame] = 10.0f * std::log10((float)std::max(squares / (to - from), 1e-12));
    }
    const double length = (double)samples.size() / sampleRate;
    for (size_t i = 0; i < detected.size(); i++){
        double start = (double)detected[i].sample / sampleRate;
        double next = i + 1 < detected.size() ? (double)detected[i + 1].sample / sampleRate : length;
        size_t first = (size_t)(start * sampleRate) / LEVEL_FRAME, last = std::min(levels.size(), (size_t)(next * sampleRate) / LEVEL_FRAME + 1);
        float loudest = -120.0f;
        for (size_t f = first; f < last; f++) loudest = std::max(loudest, levels[f]);
        double end = next;
        for (size_t f = first; f < last; f++){
            double at = (double)f * LEVEL_FRAME / sampleRate;
            if (at > start + 0.05 && (levels[f] < loudest - NOTE_DIES_DB || levels[f] < config.silenceDb)){ end = at; break; }
        }
        if (end - start < MIN_NOTE_S) continue;
        notes.push_back({ start, end, detected[i].pitch });
    }
    return notes;
}

std::vector<double> findBeats(const std::vector<HeardNote>& notes, double length){
    if (notes.size() < 4 || length <= 0.0) return {};
    // The attacks as a curve of onset strengths: a short bump at each, the longer notes a little stronger
    const int frames = (int)(length * BEAT_RATE) + 1;
    std::vector<double> onsets(frames, 0.0);
    for (const HeardNote& note : notes){
        int center = (int)std::lround(note.start * BEAT_RATE);
        double weight = 1.0 + std::min(1.0, (note.end - note.start) * 2.0);
        for (int d = -3; d <= 3; d++){
            int f = center + d;
            if (f >= 0 && f < frames) onsets[f] += weight * std::exp(-0.5 * d * d / 2.0);
        }
    }
    std::vector<double> beats = trackBeats(onsets); // the tempo, then the beats themselves (core/beats)
    if (beats.size() < 2) return {};
    // Carried on at the tempo before the first and after the last, so every note is between two beats
    while (beats.front() > notes.front().start - 1e-6) beats.insert(beats.begin(), beats.front() - (beats[1] - beats[0]));
    const double step = beats.back() - beats[beats.size() - 2];
    while (beats.back() < notes.back().end + step) beats.push_back(beats.back() + step);
    return beats;
}

// Where a time falls among the beats: beat i and the share of the way to the next
static double beatPosition(const std::vector<double>& beats, double time){
    auto after = std::upper_bound(beats.begin(), beats.end(), time);
    size_t i = after == beats.begin() ? 0 : std::min<size_t>(beats.size() - 2, (after - beats.begin()) - 1);
    return i + (time - beats[i]) / (beats[i + 1] - beats[i]);
}

bool transcribeBass(const std::vector<float>& samples, int sampleRate, const std::string& title, Transcription& out,
                    std::string& error){
    out = Transcription{};
    std::vector<HeardNote> notes = hearNotes(samples, sampleRate);
    if (notes.size() < 4){
        error = "hardly a note in it: is it a bass alone?";
        return false;
    }
    const double length = (double)samples.size() / sampleRate;
    std::vector<double> beats = findBeats(notes, length);
    if (beats.size() < 8){
        error = "no steady beat found under the notes";
        return false;
    }

    // The downbeat: of the four places in a bar, the one the notes lean on (a bass line lands on the one)
    double weight[4] = {};
    for (const HeardNote& note : notes){
        double position = beatPosition(beats, note.start);
        int nearest = (int)std::lround(position);
        if (std::fabs(position - nearest) < 0.15) weight[((nearest % 4) + 4) % 4] += 1.0 + (note.end - note.start);
    }
    const int downbeat = (int)(std::max_element(weight, weight + 4) - weight);
    // Tick 0 on the last downbeat at or before the first note (a bar more in front of the beats, if none is)
    // (a note a hair before a beat is on that beat: the beats are smoothed, the notes are where they were played)
    int firstNote = (int)std::floor(beatPosition(beats, notes.front().start) + 0.25);
    int first = firstNote - (((firstNote - downbeat) % 4) + 4) % 4;
    while (first < 0){
        for (int i = 0; i < 4; i++) beats.insert(beats.begin(), 2 * beats[0] - beats[1]);
        first += 4;
    }
    beats.erase(beats.begin(), beats.begin() + first);

    Chart& chart = out.chart;
    chart.version = 2;
    chart.title = title;
    chart.resolution = RESOLUTION;
    chart.timeSignatures = { { 0, 4, 4 } };
    chart.keys = { { 0, KeySignature{} } };
    // Tick 0 on that first beat, and the tempo beat by beat, so the chart keeps with the recording as it speeds up
    // or slows down
    fitChartToBeats(chart, beats, 0);
    out.bpm = 60.0 * (beats.size() - 1) / (beats.back() - beats.front());

    // The bass, its tuning low enough for every note: four strings, dropped to D, or five
    int lowest = 127;
    for (const HeardNote& note : notes) lowest = std::min(lowest, note.pitch);
    FrettedTrack bass;
    bass.type = InstrumentType::Bass;
    bass.name = "Bass";
    bass.tuning = lowest >= 28 ? std::vector<int>{ 28, 33, 38, 43 } : lowest >= 26 ? std::vector<int>{ 26, 33, 38, 43 }
                                                                                  : std::vector<int>{ 23, 28, 33, 38, 43 };
    // Each note on the sixteenth nearest it, held to the one nearest where it ends; on the string and fret nearest
    // where the hand was
    StringFret hand{ -1, -1 };
    for (const HeardNote& note : notes){
        int start = (int)std::lround(beatPosition(beats, note.start) * 4) * SIXTEENTH;
        int end = (int)std::lround(beatPosition(beats, note.end) * 4) * SIXTEENTH;
        if (start < 0) continue;
        bool twice = false;
        for (const FrettedNote& before : bass.notes) if (before.tick == start && bass.tuning[before.stringIndex] + before.fret == note.pitch) twice = true;
        if (twice) continue;
        std::vector<StringFret> places = positionsOf(note.pitch, bass.tuning, BASS_FRETS);
        // A string already used at that moment (two notes at once) can't take another
        places.erase(std::remove_if(places.begin(), places.end(), [&](const StringFret& place){
            for (const FrettedNote& before : bass.notes) if (before.tick == start && before.stringIndex == place.string) return true;
            return false;
        }), places.end());
        StringFret place = likeliestPosition(places, hand);
        if (place.string < 0) continue; // out of the bass's reach: an overtone heard, or another instrument's
        hand = place;
        bass.notes.push_back({ start, place.string, place.fret, std::max(SIXTEENTH, end - start) });
    }
    if (bass.notes.empty()){
        error = "none of its notes are a bass's";
        return false;
    }
    std::stable_sort(bass.notes.begin(), bass.notes.end(), [](const FrettedNote& a, const FrettedNote& b){ return a.tick < b.tick; });
    const FrettedNote& lastNote = bass.notes.back();
    chart.endTick = ((lastNote.tick + lastNote.duration) / (4 * RESOLUTION) + 1) * 4 * RESOLUTION;
    out.notes = (int)bass.notes.size();
    chart.frettedTracks = { bass };
    return true;
}
