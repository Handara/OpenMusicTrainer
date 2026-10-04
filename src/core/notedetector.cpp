#include "core/notedetector.h"

#include "core/music.h"

#include <algorithm>
#include <cmath>
#include <numeric>

const float HOP_SECONDS = 0.0027f;       // hops of ~2.7 ms: onsets are placed to within one hop, then refined
const int RECENT_HOPS = 4;               // "recent" = the last ~11 ms: a pluck jumps that fast, while a ringing
                                         // note whose level wobbles (beating, measured on a bass: 7 dB dips every
                                         // tenth of a second) climbs back far slower
const float MIN_ONSET_GAP_S = 0.05f;     // two onsets closer than this are one note (a pick's double bump)
const float ATTACK_SKIP_S = 0.01f;       // the pick's scrape right after an onset has no clear pitch: skip it
const float PITCH_TIMEOUT_S = 0.15f;     // no pitch this long after an onset: it was a noise, not a note
const int LEGATO_ANALYSIS_HOPS = 4;      // while a note rings, look for pitch changes every ~11 ms...
const int LEGATO_CONFIRMATIONS = 3;      // ...and believe a change once it holds for 3 looks in a row
// ...and once believed, wait this long for a pluck: on a real bass, the fretting hand lands on the next note (or
// mutes this one) 50 to 200 ms before that note is plucked, and the ringing string changes pitch under it. A pluck
// in that time makes it the hand getting ready, not a note; with none, it's a hammer-on or a slide, stamped when it
// happened. Measured on recordings of a bass played through a Scarlett (tests/data/bass-*.wav).
const float LEGATO_HOLD_S = 0.25f;
// An onset must not leave the sound quieter than just before it, over the time its pitch is read; and one that gives
// the pitch already ringing must make it louder by this much. A pluck of the same note again does (14 dB and more on
// the bass recorded); a finger touching the ringing string, a click of a few ms, doesn't (2.6 dB).
const float MIN_ONSET_GAIN_DB = 0.0f;
const float MIN_REPLUCK_GAIN_DB = 6.0f;
// And an onset must be this far above silence: a muted note's last breath (-50 dB on the bass recorded, its notes -15
// to -25 dB) isn't a pluck
const float ONSET_ABOVE_SILENCE_DB = 10.0f;
const int POWER_HOPS = 8;                // "just before": the last ~22 ms
const float MIN_CLARITY = 0.85f;         // how periodic a sound must be to count as a note
// A legato change must move this far from where the note started, not just round to another note: a string tuned
// between two notes (a bass's low E a quarter-tone flat) wobbles across the line between them as it rings. A slide
// of a semitone may be read from partway up, though (an F2 32 cents sharp, sliding to F#2, on a real bass)
const float LEGATO_MIN_SEMITONES = 0.6f;
// Nor may it land on one of the ringing note's own overtones (an octave, an octave and a fifth, two octaves up), a
// fifth up (the third harmonic, read an octave low) or an octave down: as a low note dies away its fundamental fades
// first, and the pitch reads as what's left. A real slide of exactly those is rare; this misreading is not.
const int OVERTONE_STEPS[] = { 7, 12, 19, 24, -12 };
// The level falls back over this time: longer than one cycle of the lowest note, so it doesn't wobble with the
// wave's shape (a 2.7 ms hop of an E3 holds under half a cycle: its RMS would rise and dip like new notes)
const float ENVELOPE_RELEASE_S = 0.05f;
const float ENVELOPE_RELEASE_CYCLES = 3.0f; // and at least this many cycles of the lowest note (a bass's E1: 73 ms)

void initNoteDetector(NoteDetector& detector, int sampleRate, const NoteDetectorConfig& config){
    detector = NoteDetector{};
    detector.config = config;
    detector.sampleRate = sampleRate;
    detector.hopSize = std::max(16, (int)(HOP_SECONDS * sampleRate));
    initPitchDetector(detector.pitch, sampleRate, config.minFrequency, config.maxFrequency);
    detector.window.assign(pitchWindowSize(detector.pitch), 0.0f);
    detector.analysisLag = detector.pitch.maxLag;
    detector.hop.reserve(detector.hopSize);
    detector.recentDb.assign(RECENT_HOPS, -120.0f);
    detector.recentPower.assign(POWER_HOPS, 0.0f);
    float release = std::max(ENVELOPE_RELEASE_S, ENVELOPE_RELEASE_CYCLES / config.minFrequency);
    detector.envelopeRelease = std::exp(-1.0f / (release * sampleRate));
}

// Envelope follower: rises instantly with the signal, falls slowly. Returns its level at the end of the hop, in dB.
static float followEnvelope(NoteDetector& detector, const std::vector<float>& samples){
    for (float s : samples) detector.envelope = std::max(std::fabs(s), detector.envelope * detector.envelopeRelease);
    return 20.0f * std::log10(std::max(detector.envelope, 1e-6f));
}

void expectLowestFrequency(NoteDetector& detector, float frequency){
    int lag = detector.pitch.maxLag;
    // A little below the note, for one played flat
    if (frequency > 0.0f) lag = (int)std::ceil(detector.sampleRate / (frequency * 0.9f));
    detector.analysisLag = std::clamp(lag, std::min(detector.pitch.minLag + 4, detector.pitch.maxLag), detector.pitch.maxLag);
}

// The pitch of the latest samples (two of the longest periods looked for), as a fractional MIDI number; negative if
// there's no clear pitch
static float analyzePitch(NoteDetector& detector){
    const int count = 2 * detector.analysisLag;
    const float* latest = detector.window.data() + detector.window.size() - count;
    PitchResult result = detectPitch(detector.pitch, latest, count, detector.analysisLag);
    if (result.frequency <= 0.0f || result.clarity < MIN_CLARITY) return -1.0f;
    return frequencyToMidi(result.frequency);
}

static void emit(NoteDetector& detector, long long sample, float midi, std::vector<DetectedNote>& out){
    int pitch = (int)std::lround(midi);
    out.push_back({sample, pitch, (midi - pitch) * 100.0f});
    detector.sounding = true;
    detector.currentPitch = pitch;
    detector.currentMidi = midi;
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

    float level = followEnvelope(detector, hop);
    float power = 0.0f;
    for (float s : hop) power += s * s;
    power /= (float)hopSize;
    const float powerBefore = std::accumulate(detector.recentPower.begin(), detector.recentPower.end(), 0.0f) / POWER_HOPS;
    detector.recentPower.erase(detector.recentPower.begin());
    detector.recentPower.push_back(power);
    if (detector.pitchPending){
        detector.sinceOnsetPower += power;
        detector.sinceOnsetHops++;
    }
    float recentMin = *std::min_element(detector.recentDb.begin(), detector.recentDb.end());
    detector.recentDb.erase(detector.recentDb.begin());
    detector.recentDb.push_back(level);

    // A held legato change with no pluck since: a note after all
    if (detector.legatoHeld && detector.position >= detector.legatoDue){
        int pitch = (int)std::lround(detector.legatoMidi);
        out.push_back({detector.legatoSample, pitch, (detector.legatoMidi - pitch) * 100.0f, true});
        detector.legatoHeld = false;
    }

    if (level < detector.config.silenceDb){
        detector.sounding = false; // the note has died away
        return;
    }

    // Onset: a sudden rise. Its exact sample: the first one inside this hop reaching 20% of the hop's peak.
    bool rise = level - recentMin >= detector.config.onsetRiseDb && level >= detector.config.silenceDb + ONSET_ABOVE_SILENCE_DB;
    long long gapSamples = (long long)(MIN_ONSET_GAP_S * detector.sampleRate);
    if (rise && hopStart - detector.lastOnsetSample >= gapSamples){
        float peak = 0.0f;
        for (float s : hop) peak = std::max(peak, std::fabs(s));
        int offset = 0;
        while (offset < hopSize - 1 && std::fabs(hop[offset]) < 0.2f * peak) offset++;
        detector.onsetSample = hopStart + offset;
        detector.lastOnsetSample = detector.onsetSample;
        detector.attacks.push_back(detector.onsetSample);
        detector.legatoHeld = false; // the hand was getting ready for this pluck
        detector.beforeOnsetPower = powerBefore;
        detector.sinceOnsetPower = power;
        detector.sinceOnsetHops = 1;
        detector.soundingBeforeOnset = detector.sounding;
        detector.pitchPending = true;
        detector.sounding = false;
    }

    if (detector.pitchPending){
        // Wait for a full window of sound after the attack, then look for the pitch until the timeout
        long long ready = detector.onsetSample + (long long)(ATTACK_SKIP_S * detector.sampleRate) + 2LL * detector.analysisLag;
        if (detector.position < ready) return;
        float midi = analyzePitch(detector);
        // Hardly louder than before it: a touch or a click on a string that rings on, not a pluck
        const float gain = 10.0f * std::log10(std::max(1e-12, detector.sinceOnsetPower / detector.sinceOnsetHops) / std::max(1e-12f, detector.beforeOnsetPower));
        const bool samePitch = detector.soundingBeforeOnset && midi >= 0.0f && (int)std::lround(midi) == detector.currentPitch;
        if (gain < (samePitch ? MIN_REPLUCK_GAIN_DB : MIN_ONSET_GAIN_DB)){
            detector.pitchPending = false;
            detector.sounding = detector.soundingBeforeOnset;
            return;
        }
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
    bool overtone = std::count(std::begin(OVERTONE_STEPS), std::end(OVERTONE_STEPS), pitch - detector.currentPitch) > 0;
    if (pitch == detector.currentPitch || overtone || std::fabs(midi - detector.currentMidi) < LEGATO_MIN_SEMITONES){
        detector.candidateCount = 0;
        return;
    }
    if (pitch != detector.candidatePitch || detector.candidateCount == 0){
        detector.candidatePitch = pitch;
        detector.candidateCount = 0;
        // The change happened somewhere in this window: its middle is the best guess
        detector.candidateSample = detector.position - detector.analysisLag;
    }
    if (++detector.candidateCount >= LEGATO_CONFIRMATIONS){
        // It's ringing at this pitch now (so it isn't confirmed again), but only told once no pluck follows
        detector.currentPitch = pitch;
        detector.currentMidi = midi;
        detector.candidateCount = 0;
        detector.legatoHeld = true;
        detector.changes.push_back(detector.candidateSample);
        detector.legatoSample = detector.candidateSample;
        detector.legatoMidi = midi;
        detector.legatoDue = detector.candidateSample + (long long)(LEGATO_HOLD_S * detector.sampleRate);
    }
}

long long noteDetectorPending(const NoteDetector& detector){
    if (detector.pitchPending) return detector.onsetSample;
    if (detector.legatoHeld) return detector.legatoSample;
    if (detector.sounding && detector.candidateCount > 0) return detector.candidateSample; // a change, not yet sure
    return -1;
}

bool noteDetectorHeldChange(const NoteDetector& detector, long long& sample, int& pitch){
    if (!detector.legatoHeld) return false;
    sample = detector.legatoSample;
    pitch = (int)std::lround(detector.legatoMidi);
    return true;
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
