#include "core/notedetector.h"

#include "core/music.h"

#include <algorithm>
#include <cmath>

const float HOP_SECONDS = 0.0027f;       // hops of ~2.7 ms: onsets are placed to within one hop, then refined
const int RECENT_HOPS = 16;              // "recent" = the last ~43 ms
const float MIN_ONSET_GAP_S = 0.05f;     // two onsets closer than this are one note (a pick's double bump)
const float ATTACK_SKIP_S = 0.01f;       // the pick's scrape right after an onset has no clear pitch: skip it
const float PITCH_TIMEOUT_S = 0.15f;     // no pitch this long after an onset: it was a noise, not a note
const int LEGATO_ANALYSIS_HOPS = 4;      // while a note rings, look for pitch changes every ~11 ms...
const int LEGATO_CONFIRMATIONS = 3;      // ...and believe a change once it holds for 3 looks in a row
const float MIN_CLARITY = 0.85f;         // how periodic a sound must be to count as a note

void initNoteDetector(NoteDetector& detector, int sampleRate, const NoteDetectorConfig& config){
    detector = NoteDetector{};
    detector.config = config;
    detector.sampleRate = sampleRate;
    detector.hopSize = std::max(16, (int)(HOP_SECONDS * sampleRate));
    initPitchDetector(detector.pitch, sampleRate, config.minFrequency, config.maxFrequency);
    detector.window.assign(pitchWindowSize(detector.pitch), 0.0f);
    detector.hop.reserve(detector.hopSize);
    detector.recentDb.assign(RECENT_HOPS, -120.0f);
}

static float levelDb(const float* samples, int count){
    float sumSquares = 0.0f;
    for (int i = 0; i < count; i++) sumSquares += samples[i] * samples[i];
    return 20.0f * std::log10(std::max(std::sqrt(sumSquares / count), 1e-6f));
}

// The pitch of the latest window, as a fractional MIDI number; negative if there's no clear pitch
static float analyzePitch(NoteDetector& detector){
    PitchResult result = detectPitch(detector.pitch, detector.window.data(), (int)detector.window.size());
    if (result.frequency <= 0.0f || result.clarity < MIN_CLARITY) return -1.0f;
    return frequencyToMidi(result.frequency);
}

static void emit(NoteDetector& detector, long long sample, float midi, std::vector<DetectedNote>& out){
    int pitch = (int)std::lround(midi);
    out.push_back({sample, pitch, (midi - pitch) * 100.0f});
    detector.sounding = true;
    detector.currentPitch = pitch;
    detector.candidateCount = 0;
}

static void processHop(NoteDetector& detector, std::vector<DetectedNote>& out){
    const std::vector<float>& hop = detector.hop;
    const int hopSize = (int)hop.size();
    const long long hopStart = detector.position - hopSize;

    // Slide the analysis window forward by one hop
    std::vector<float>& window = detector.window;
    std::move(window.begin() + hopSize, window.end(), window.begin());
    std::copy(hop.begin(), hop.end(), window.end() - hopSize);

    float level = levelDb(hop.data(), hopSize);
    float recentMin = *std::min_element(detector.recentDb.begin(), detector.recentDb.end());
    detector.recentDb.erase(detector.recentDb.begin());
    detector.recentDb.push_back(level);

    if (level < detector.config.silenceDb){
        detector.sounding = false; // the note has died away
        return;
    }

    // Onset: a sudden rise. Its exact sample: the first one inside this hop reaching 20% of the hop's peak.
    bool rise = level - recentMin >= detector.config.onsetRiseDb;
    long long gapSamples = (long long)(MIN_ONSET_GAP_S * detector.sampleRate);
    if (rise && hopStart - detector.lastOnsetSample >= gapSamples){
        float peak = 0.0f;
        for (float s : hop) peak = std::max(peak, std::fabs(s));
        int offset = 0;
        while (offset < hopSize - 1 && std::fabs(hop[offset]) < 0.2f * peak) offset++;
        detector.onsetSample = hopStart + offset;
        detector.lastOnsetSample = detector.onsetSample;
        detector.pitchPending = true;
        detector.sounding = false;
    }

    if (detector.pitchPending){
        // Wait for a full window of sound after the attack, then look for the pitch until the timeout
        long long ready = detector.onsetSample + (long long)(ATTACK_SKIP_S * detector.sampleRate) + (long long)window.size();
        if (detector.position < ready) return;
        float midi = analyzePitch(detector);
        if (midi >= 0.0f){
            emit(detector, detector.onsetSample, midi, out);
            detector.pitchPending = false;
        } else if (detector.position - detector.onsetSample > (long long)(PITCH_TIMEOUT_S * detector.sampleRate)){
            detector.pitchPending = false; // no pitch: a knock or a scrape, not a note
        }
        return;
    }

    // Legato: while a note rings, a steady new pitch without a new attack is a new note
    if (!detector.sounding || ++detector.hopsSinceAnalysis < LEGATO_ANALYSIS_HOPS) return;
    detector.hopsSinceAnalysis = 0;
    float midi = analyzePitch(detector);
    if (midi < 0.0f) return;
    int pitch = (int)std::lround(midi);
    if (pitch == detector.currentPitch){
        detector.candidateCount = 0;
        return;
    }
    if (pitch != detector.candidatePitch || detector.candidateCount == 0){
        detector.candidatePitch = pitch;
        detector.candidateCount = 0;
        // The change happened somewhere in this window: its middle is the best guess
        detector.candidateSample = detector.position - (long long)window.size() / 2;
    }
    if (++detector.candidateCount >= LEGATO_CONFIRMATIONS) emit(detector, detector.candidateSample, midi, out);
}

void feedNoteDetector(NoteDetector& detector, const float* samples, int count, std::vector<DetectedNote>& out){
    for (int i = 0; i < count; i++){
        detector.hop.push_back(samples[i]);
        detector.position++;
        if ((int)detector.hop.size() == detector.hopSize){
            processHop(detector, out);
            detector.hop.clear();
        }
    }
}
