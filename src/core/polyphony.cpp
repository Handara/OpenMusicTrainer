#include "core/polyphony.h"

#include "core/fft.h"
#include "core/music.h"

#include <algorithm>
#include <cmath>
#include <complex>

const float PI_F = 3.14159265358979f;
const float TOP_HZ = 5000.0f;          // harmonics are looked for up to here: above, a string has little left
const float QUIET = 0.002f;            // a sound whose strongest peak is under this (-54 dB) is silence
const float PARTIAL_FLOOR = 0.015f;    // peaks under this share of the strongest are left out
const float RIPPLE_REACH = 3.2f;       // the window draws ripples beside a strong peak: within this many times the
const float RIPPLE_SHARE = 0.045f;     // sound's own resolution (1 / its length) of it, and under this share of it
const float HIDDEN_REACH = 2.4f;       // two peaks nearer than this many times the resolution show as one: a
                                       // harmonic that close to a stronger peak may be there, unseen
const int MAX_HARMONICS = 10;
const int MAX_DIVISOR = 5;             // a peak may be a note's 1st to 5th harmonic: each is tried as that note
const float LOOSE_FIT = 0.03f;         // a peak this near a harmonic's place (half a semitone) is that harmonic...
const float TIGHT_FIT = 0.015f;        // ...and this near, once the note's own tuning has been measured from them
const float SAME_SCORE = 0.95f;        // of two readings this alike, the higher note is the one (see readNote)
const float MIN_SHARE = 0.2f;          // a further note is this strong beside the first: less is a string ringing
                                       // in sympathy, or what's left over of the first
const int FIT_HARMONICS = 6;           // a note's first harmonics are the ones sure to be there: empty places are
const float MIN_FIT = 0.6f;            // counted among these, and a note has this share of them or it isn't one
const int MIN_OWN_HARMONICS = 2;       // of which this many its own, not shared with a note already found
const float OCTAVE_DIP = 0.4f;         // an odd harmonic under this share of the even ones beside it has dipped: with
                                       // every odd one dipped, an octave above is sounding too
const float NEW_NOTE_RISE = 1.5f;      // a note this much stronger after a pluck than before it was plucked now
const float LEGATO_AFTER_S = 0.05f;    // a note with no attack, later than this after a pluck, followed it
const float HISTORY_S = 1.0f;          // sound kept, for listening before and after a pluck

namespace {

struct Partial {
    float hz, amplitude;
    bool taken; // by a note already found
};

// A note read off the peaks: how well a row of harmonics on `hz` fits them
struct Reading {
    float hz = 0.0f;
    float strength = 0.0f; // its own harmonics' amplitudes, added up
    int own = 0;           // how many of them there are
    float fit = 0.0f;      // the share of its first harmonics that are there (up to the highest one found)
    bool shares = false;   // some of its harmonics' places are another note's
    float score = 0.0f;
    float harmonics[MAX_HARMONICS + 1] = {}; // each one's amplitude, by its number; 0 where there's none
};

// The peak nearest a frequency (they're sorted by frequency)
const Partial* nearest(const std::vector<Partial>& partials, float hz){
    auto after = std::lower_bound(partials.begin(), partials.end(), hz, [](const Partial& p, float value){ return p.hz < value; });
    if (after == partials.end()) return &partials.back();
    if (after == partials.begin()) return &*after;
    return hz - (after - 1)->hz < after->hz - hz ? &*(after - 1) : &*after;
}

// A row of harmonics is a note's when most of the row is there. A row an octave under the real note fits too (the
// real harmonics are its even ones) but with every odd place empty; a row an octave over fits with half of the
// peaks left unexplained. So the score is the amplitude accounted for, cut hard by the share of places found empty.
// Places already taken by another note count neither way, nor do places a stronger peak sits too close to for a
// harmonic there to show (`hidden`, in hertz).
Reading readNote(const std::vector<Partial>& partials, float hz, float topHz, float hidden){
    Reading reading;
    for (float tolerance : { LOOSE_FIT, TIGHT_FIT }){
        reading = Reading{};
        reading.hz = hz;
        float tuned = 0.0f, weight = 0.0f;
        int found = 0, empty = 0, emptySinceFound = 0;
        for (int h = 1; h <= MAX_HARMONICS && hz * h <= topHz; h++){
            const Partial* peak = nearest(partials, hz * h);
            if (std::fabs(peak->hz - hz * h) > tolerance * hz * h){
                if (std::fabs(peak->hz - hz * h) <= hidden) reading.shares = true;
                else if (h <= FIT_HARMONICS) emptySinceFound++;
                continue;
            }
            if (peak->taken){
                reading.shares = true;
                continue;
            }
            reading.harmonics[h] = peak->amplitude;
            reading.strength += peak->amplitude;
            reading.own++;
            tuned += peak->amplitude * peak->hz / h;
            weight += peak->amplitude;
            if (h <= FIT_HARMONICS) found++;
            empty += emptySinceFound; // empty places count only up to the highest harmonic found
            emptySinceFound = 0;
        }
        if (weight <= 0.0f) return reading;
        reading.fit = found + empty > 0 ? (float)found / (found + empty) : 0.0f;
        reading.score = reading.fit >= MIN_FIT ? reading.strength * reading.fit * reading.fit : 0.0f;
        hz = tuned / weight; // where its harmonics say the note really is: read again from there, more strictly
    }
    return reading;
}

// Whether a note's harmonics say its octave is sounding with it. The upper note's harmonics all fall on the lower
// one's even ones, so those stand well over the odd ones between them, all the way up. One odd harmonic can be weak
// in a note alone (where the string is plucked, where its pickup sits); all of them aren't.
bool octaveAbove(const Reading& note){
    if (note.shares) return false; // its harmonics aren't all its own: nothing to read from them
    int dipped = 0;
    for (int h = 3; h + 1 <= MAX_HARMONICS; h += 2){
        float below = note.harmonics[h - 1], above = note.harmonics[h + 1];
        if (below <= 0.0f && above <= 0.0f) continue; // nothing either side to compare it with
        if (note.harmonics[h] > OCTAVE_DIP * 0.5f * (below + above)) return false;
        dipped++;
    }
    return dipped >= 2 && note.harmonics[2] > note.harmonics[1];
}

} // namespace

std::vector<HeardPitch> notesInSound(const float* samples, int count, int sampleRate, int lowestPitch, int highestPitch,
                                     int maxNotes){
    std::vector<HeardPitch> heard;
    if (count < 64 || sampleRate <= 0 || highestPitch < lowestPitch) return heard;

    // The spectrum: the sound faded in and out (a Hann window), with silence after it to twice its length or more,
    // which draws each peak with more points
    int size = 1;
    while (size < 2 * count) size <<= 1;
    std::vector<std::complex<float>> spectrum(size);
    float windowSum = 0.0f;
    for (int i = 0; i < count; i++){
        float window = 0.5f - 0.5f * std::cos(2.0f * PI_F * i / (count - 1));
        spectrum[i] = samples[i] * window;
        windowSum += window;
    }
    fft(spectrum, fftTwiddles(size), false);

    const float hzPerBin = (float)sampleRate / size;
    const float lowHz = midiToFrequency(lowestPitch - 0.5f), highHz = midiToFrequency(highestPitch + 0.5f);
    const float topHz = std::min(TOP_HZ, sampleRate * 0.45f);
    const int firstBin = std::max(2, (int)(lowHz * 0.9f / hzPerBin)), lastBin = std::min(size / 2 - 2, (int)(topHz / hzPerBin));
    if (lastBin <= firstBin) return heard;
    std::vector<float> magnitude(lastBin + 2);
    float strongest = 0.0f;
    for (int k = firstBin - 1; k <= lastBin + 1; k++){
        magnitude[k] = std::abs(spectrum[k]) * 2.0f / windowSum; // a sine comes out as its own amplitude
        strongest = std::max(strongest, magnitude[k]);
    }
    if (strongest < QUIET) return heard;

    // Its peaks, each placed between bins by the parabola through its top three points
    std::vector<Partial> partials;
    for (int k = firstBin; k <= lastBin; k++){
        float a = magnitude[k - 1], b = magnitude[k], c = magnitude[k + 1];
        if (b <= a || b < c || b < PARTIAL_FLOOR * strongest) continue;
        float bend = a - 2.0f * b + c, shift = bend < 0.0f ? 0.5f * (a - c) / bend : 0.0f;
        partials.push_back({ (k + shift) * hzPerBin, b - 0.25f * (a - c) * shift, false });
    }
    const float reach = RIPPLE_REACH * sampleRate / count;
    auto ripple = [&](const Partial& peak){
        return std::any_of(partials.begin(), partials.end(), [&](const Partial& other){
            return std::fabs(other.hz - peak.hz) <= reach && peak.amplitude < RIPPLE_SHARE * other.amplitude;
        });
    };
    std::vector<Partial> real;
    for (const Partial& peak : partials) if (!ripple(peak)) real.push_back(peak);
    partials = real;
    if (partials.empty()) return heard;

    float firstStrength = 0.0f;
    while ((int)heard.size() < maxNotes){
        // Every note a peak left could be a harmonic of; the best reading, the higher of two that fit alike
        Reading best;
        for (const Partial& peak : partials){
            if (peak.taken) continue;
            for (int divisor = 1; divisor <= MAX_DIVISOR; divisor++){
                float hz = peak.hz / divisor;
                if (hz < lowHz || hz > highHz) continue;
                Reading reading = readNote(partials, hz, topHz, HIDDEN_REACH * sampleRate / count);
                bool higher = reading.hz > best.hz * 1.03f;
                if (reading.score > best.score * (higher ? SAME_SCORE : 1.0f / SAME_SCORE)) best = reading;
            }
        }
        if (best.score <= 0.0f) break;
        int pitch = (int)std::lround(frequencyToMidi(best.hz));
        if (pitch < lowestPitch || pitch > highestPitch) break;
        if (heard.empty()) firstStrength = best.strength;
        else if (best.strength < MIN_SHARE * firstStrength || best.own < MIN_OWN_HARMONICS) break;
        // A note beside one already found (the same, or a half step off) is that note again, read off its leftovers:
        // their harmonics are too close to show apart in so short a sound
        bool again = std::any_of(heard.begin(), heard.end(), [&](const HeardPitch& note){ return std::abs(note.pitch - pitch) <= 1; });
        if (!again) heard.push_back({ pitch, best.strength });

        bool octaveHeard = std::any_of(heard.begin(), heard.end(), [&](const HeardPitch& note){ return note.pitch == pitch + 12; });
        if (!again && !octaveHeard && pitch + 12 <= highestPitch && (int)heard.size() < maxNotes && octaveAbove(best)){
            float even = 0.0f;
            for (int h = 2; h <= MAX_HARMONICS; h += 2) even += best.harmonics[h];
            heard.push_back({ pitch + 12, even });
        }
        // All of its harmonics are set aside, however high
        for (Partial& peak : partials){
            float harmonic = std::round(peak.hz / best.hz);
            if (harmonic >= 1.0f && std::fabs(peak.hz - harmonic * best.hz) <= LOOSE_FIT * peak.hz) peak.taken = true;
        }
    }
    std::sort(heard.begin(), heard.end(), [](const HeardPitch& a, const HeardPitch& b){ return a.pitch < b.pitch; });
    return heard;
}

void initPluckListener(PluckListener& listener, int sampleRate, int lowestPitch, int highestPitch){
    listener = PluckListener{};
    listener.sampleRate = sampleRate;
    listener.lowestPitch = lowestPitch;
    listener.highestPitch = highestPitch;
    listener.listen = (int)(NOTES_LISTEN_S * sampleRate);
}

void feedPluckListener(PluckListener& listener, const float* samples, int count, const std::vector<long long>& attacks,
                       const std::vector<DetectedNote>& notes, std::vector<PluckNotes>& out){
    listener.recent.insert(listener.recent.end(), samples, samples + count);
    listener.position += count;
    const size_t kept = (size_t)(HISTORY_S * listener.sampleRate);
    if (listener.recent.size() > 2 * kept) listener.recent.erase(listener.recent.begin(), listener.recent.end() - kept);
    const long long oldest = listener.position - (long long)listener.recent.size();

    for (long long attack : attacks){
        listener.plucks.push_back(attack);
        listener.attacks.push_back(attack);
    }
    if (listener.attacks.size() > 32) listener.attacks.erase(listener.attacks.begin(), listener.attacks.end() - 16);
    for (const DetectedNote& note : notes){
        bool plucked = std::count(listener.attacks.begin(), listener.attacks.end(), note.sample) > 0;
        if (!plucked) listener.changes.push_back(note.sample);
    }
    if (listener.changes.size() > 32) listener.changes.erase(listener.changes.begin(), listener.changes.end() - 16);

    const int listen = listener.listen;
    auto between = [](const std::vector<long long>& samples, long long from, long long to){
        return std::any_of(samples.begin(), samples.end(), [&](long long sample){ return sample > from && sample < to; });
    };
    while (!listener.plucks.empty() && listener.position >= listener.plucks.front() + listen){
        const long long pluck = listener.plucks.front();
        listener.plucks.erase(listener.plucks.begin());
        if (pluck < oldest) continue; // its sound is gone: the game stood still too long
        // Another pluck, or a note following without one, before enough was heard: notes one after the other
        if (between(listener.attacks, pluck, pluck + listen)) continue;
        if (between(listener.changes, pluck + (long long)(LEGATO_AFTER_S * listener.sampleRate), pluck + listen)) continue;

        const float* sound = listener.recent.data() + (pluck - oldest);
        std::vector<HeardPitch> now = notesInSound(sound, listen, listener.sampleRate, listener.lowestPitch, listener.highestPitch);
        if (now.size() < 2) continue;
        std::vector<HeardPitch> before;
        if (pluck - listen >= oldest) before = notesInSound(sound - listen, listen, listener.sampleRate, listener.lowestPitch, listener.highestPitch);
        PluckNotes found{ pluck, {} };
        for (const HeardPitch& note : now){
            auto was = std::find_if(before.begin(), before.end(), [&](const HeardPitch& old){ return old.pitch == note.pitch; });
            if (was == before.end() || note.strength >= NEW_NOTE_RISE * was->strength) found.pitches.push_back(note.pitch);
        }
        if (found.pitches.size() >= 2) out.push_back(found);
    }
}
