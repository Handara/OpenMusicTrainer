#include "core/backingband.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <random>

namespace {

// The key's chords, as degrees from its tonic: a major key's I ii iii IV V vi (the diminished vii left out), a minor
// key's i III iv V VI VII (its V major, borrowed from the harmonic minor: it pulls home)
struct KeyChord {
    int offset;   // from the tonic, in semitones
    bool minor;
    char function; // 'T' home, 'S' away (subdominant), 'D' tension (dominant)
};
const KeyChord MAJOR_CHORDS[] = { { 0, false, 'T' }, { 2, true, 'S' }, { 4, true, 'T' }, { 5, false, 'S' }, { 7, false, 'D' }, { 9, true, 'T' } };
const KeyChord MINOR_CHORDS[] = { { 0, true, 'T' }, { 3, false, 'T' }, { 5, true, 'S' }, { 7, false, 'D' }, { 8, false, 'S' }, { 10, false, 'D' } };
const int KEY_CHORDS = 6;

// How well a note sits over a chord, by its interval from the chord's root: its own notes best, the gentle colours
// (a 9th, a 6th) fine, the notes that rub (a minor 2nd above a chord note, a 4th over a major third) worst
const float MAJOR_FIT[12] = { 2.0f, -2.0f, 0.4f, -1.5f, 2.0f, -1.2f, -1.0f, 2.0f, -1.2f, 0.4f, -0.6f, 0.2f };
const float MINOR_FIT[12] = { 2.0f, -2.0f, 0.4f, 2.0f, -1.5f, 0.3f, -1.0f, 2.0f, -1.5f, -0.5f, 0.4f, -1.0f };

int tonicOf(const KeySignature& key){
    return (((key.fifths * 7 + (key.minor ? 9 : 0)) % 12) + 12) % 12;
}

// A move from one chord to the next, as songs make them: away from home, to the tension, home again
float moveScore(const KeyChord& from, const KeyChord& to, bool same){
    if (same) return -0.4f; // allowed, but a progression goes somewhere
    const char a = from.function, b = to.function;
    if (a == 'T' && b == 'S') return 0.8f;
    if (a == 'S' && b == 'D') return 1.0f;
    if (a == 'D' && b == 'T') return to.offset == 0 ? 1.4f : 0.6f; // home, or deceptively elsewhere
    if (a == 'T' && b == 'D') return 0.5f;
    if (a == 'S' && b == 'T') return 0.5f;
    if (a == 'T' && b == 'T') return 0.1f;
    if (a == 'S' && b == 'S') return 0.2f;
    return -0.3f; // tension back to the subdominant: possible, weaker
}

const char* const SHARP_NAMES[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
const char* const FLAT_NAMES[12] = { "C", "Db", "D", "Eb", "E", "F", "Gb", "G", "Ab", "A", "Bb", "B" };

// --- Writing the parts ---------------------------------------------------------------------------------------

// What a style's bar needs: where it is, its chord (and the next), and the hits it adds
struct BarWriter {
    std::vector<BandHit>* hits;
    double start;          // the bar's first beat
    int beats;
    int bar, bars;         // which bar, of how many
    BandChord chord, next;
    int bassRoot;          // the chord's root, in the bass's register
    int nextBassRoot;
    std::vector<int> voicing; // the keys' chord, voiced near the last
    float swing8 = 0.0f, swing16 = 0.0f;
    std::mt19937* random;

    double swung(double beat) const {
        const double whole = std::floor(beat), part = beat - whole;
        if (swing8 > 0.0f && std::fabs(part - 0.5) < 1e-6) return whole + 0.5 + swing8 / 6.0;   // toward a triplet's last
        if (swing16 > 0.0f && (std::fabs(part - 0.25) < 1e-6 || std::fabs(part - 0.75) < 1e-6)) return beat + swing16 / 12.0;
        return beat;
    }
    float humanize(float velocity){
        std::uniform_real_distribution<float> wobble(-0.07f, 0.07f);
        return std::clamp(velocity + wobble(*random), 0.05f, 1.0f);
    }
    void drum(double beat, KitDrum which, float velocity){
        BandHit hit;
        hit.part = BandPart::Drums;
        hit.beat = start + swung(beat);
        hit.drum = which;
        hit.velocity = humanize(velocity);
        hits->push_back(hit);
    }
    void bass(double beat, double length, int pitch, float velocity){
        BandHit hit;
        hit.part = BandPart::Bass;
        hit.beat = start + swung(beat);
        hit.length = length;
        hit.pitch = pitch;
        hit.velocity = humanize(velocity);
        hits->push_back(hit);
    }
    void keys(double beat, double length, float velocity){
        for (int pitch : voicing){
            BandHit hit;
            hit.part = BandPart::Keys;
            hit.beat = start + swung(beat);
            hit.length = length;
            hit.pitch = pitch;
            hit.velocity = humanize(velocity);
            hits->push_back(hit);
        }
    }
    int third() const { return bassRoot + (chord.minor ? 3 : 4); }
    int fifth() const { return bassRoot + 7; }
    int octave() const { return bassRoot + 12; }
    int sixth() const { return bassRoot + (chord.minor ? 8 : 9); }
    int seventh() const { return bassRoot + 10; }
    // A note leading into the next bar's root: a half step below it, or a step above
    int approach() const { return nextBassRoot + ((bar % 2) ? 2 : -1); }
    bool phraseEnd() const { return bar % 4 == 3 || bar + 1 == bars; } // a fill: the end of four bars, or of the song
};

// The styles. Each writes one bar; beats are quarter notes from the bar's start.
struct Style {
    const char* name;
    int lowTempo, highTempo; // where it grooves (bpm)
    int beats;               // the bars it's for (0: any)
    bool sevenths;           // its chords with their sevenths (funk, hip-hop, bossa...)
    float swing8, swing16;
    std::function<void(BarWriter&)> write;
};

void snareFill(BarWriter& w, double from){
    for (double beat = from; beat < w.beats - 0.01; beat += 0.25) w.drum(beat, KitDrum::Snare, 0.35f + 0.5f * (float)((beat - from) / (w.beats - from)));
}

const std::vector<Style>& styles(){
    static const std::vector<Style> STYLES = {
        { "Rock", 90, 180, 4, false, 0.0f, 0.0f, [](BarWriter& w){
            for (int i = 0; i < 8; i++) w.drum(i * 0.5, KitDrum::Hat, i % 2 ? 0.35f : 0.6f);
            w.drum(0, KitDrum::Kick, 0.9f);
            w.drum(2, KitDrum::Kick, 0.8f);
            if (w.bar % 2) w.drum(2.5, KitDrum::Kick, 0.6f);
            w.drum(1, KitDrum::Snare, 0.85f);
            if (w.phraseEnd()) snareFill(w, 3.0); else w.drum(3, KitDrum::Snare, 0.85f);
            for (int i = 0; i < 7; i++) w.bass(i * 0.5, 0.45, i == 5 ? w.fifth() : w.bassRoot, i % 2 ? 0.6f : 0.8f);
            w.bass(3.5, 0.45, w.approach(), 0.65f);
            w.keys(0, 1.9, 0.45f);
            w.keys(2, 1.9, 0.4f);
        } },
        { "Funk", 80, 120, 4, true, 0.0f, 0.0f, [](BarWriter& w){
            const float hat[4] = { 0.55f, 0.25f, 0.4f, 0.25f };
            for (int i = 0; i < 16; i++) w.drum(i * 0.25, KitDrum::Hat, hat[i % 4]);
            w.drum(0, KitDrum::Kick, 0.95f);
            w.drum(0.75, KitDrum::Kick, 0.6f);
            w.drum(2.5, KitDrum::Kick, 0.8f);
            if (w.bar % 2) w.drum(3.25, KitDrum::Kick, 0.55f);
            w.drum(1, KitDrum::Snare, 0.9f);
            w.drum(1.75, KitDrum::Snare, 0.2f); // ghost notes
            w.drum(2.25, KitDrum::Snare, 0.18f);
            if (w.phraseEnd()) snareFill(w, 3.0); else w.drum(3, KitDrum::Snare, 0.9f);
            w.bass(0, 0.4, w.bassRoot, 0.95f);
            w.bass(0.75, 0.2, w.octave(), 0.7f);
            w.bass(1.5, 0.3, w.bassRoot, 0.75f);
            w.bass(2.0, 0.25, w.seventh(), 0.6f);
            w.bass(2.5, 0.4, w.bassRoot, 0.85f);
            w.bass(3.0, 0.2, w.octave(), 0.65f);
            w.bass(3.5, 0.2, w.fifth(), 0.6f);
            w.bass(3.75, 0.2, w.approach(), 0.6f);
            for (double beat : { 0.5, 1.75, 2.75, 3.5 }) w.keys(beat, 0.2, 0.4f);
        } },
        { "Reggae", 60, 100, 4, false, 0.25f, 0.0f, [](BarWriter& w){
            for (int i = 0; i < 8; i++) w.drum(i * 0.5, KitDrum::Hat, i % 2 ? 0.25f : 0.4f);
            w.drum(2, KitDrum::Kick, 0.9f); // the one drop: kick and rim together on the third beat
            w.drum(2, KitDrum::Snare, 0.5f);
            if (w.phraseEnd()) w.drum(3.5, KitDrum::OpenHat, 0.4f);
            w.bass(0, 0.9, w.bassRoot, 0.9f);
            w.bass(1.5, 0.4, w.bassRoot, 0.7f);
            w.bass(2.5, 0.4, w.fifth(), 0.75f);
            w.bass(3, 0.4, w.third(), 0.7f);
            w.bass(3.5, 0.4, w.approach(), 0.6f);
            for (double beat : { 1.0, 3.0 }) w.keys(beat, 0.3, 0.45f); // the skank, on the backbeats
        } },
        { "Disco", 110, 135, 4, true, 0.0f, 0.0f, [](BarWriter& w){
            for (int i = 0; i < 4; i++){
                w.drum(i, KitDrum::Kick, 0.9f);
                w.drum(i + 0.5, KitDrum::OpenHat, 0.35f);
                w.drum(i, KitDrum::Hat, 0.3f);
            }
            w.drum(1, KitDrum::Snare, 0.8f);
            w.drum(3, KitDrum::Snare, 0.8f);
            if (w.phraseEnd()) snareFill(w, 3.5);
            for (int i = 0; i < 8; i++) w.bass(i * 0.5, 0.35, i % 2 ? w.octave() : w.bassRoot, i % 2 ? 0.65f : 0.85f);
            for (double beat : { 0.5, 1.5, 2.5, 3.5 }) w.keys(beat, 0.25, 0.35f);
        } },
        { "Hip-hop", 70, 100, 4, true, 0.0f, 0.6f, [](BarWriter& w){
            for (int i = 0; i < 8; i++) w.drum(i * 0.5, KitDrum::Hat, i % 2 ? 0.3f : 0.5f);
            w.drum(0, KitDrum::Kick, 0.95f);
            w.drum(1.75, KitDrum::Kick, 0.6f);
            w.drum(2.5, KitDrum::Kick, 0.85f);
            w.drum(1, KitDrum::Snare, 0.9f);
            w.drum(3, KitDrum::Snare, 0.9f);
            if (w.phraseEnd()) w.drum(3.75, KitDrum::Snare, 0.3f);
            w.bass(0, 1.5, w.bassRoot, 0.9f);
            w.bass(1.75, 0.5, w.bassRoot, 0.7f);
            w.bass(2.5, 1.0, w.fifth(), 0.75f);
            w.bass(3.5, 0.45, w.approach(), 0.6f);
            w.keys(0, 3.4, 0.35f);
            w.keys(2.75, 0.5, 0.25f);
        } },
        { "Bossa", 100, 150, 4, true, 0.0f, 0.0f, [](BarWriter& w){
            for (int i = 0; i < 8; i++) w.drum(i * 0.5, KitDrum::Hat, 0.25f);
            for (double beat : { 0.0, 1.5, 2.0, 3.5 }) w.drum(beat, KitDrum::Kick, 0.55f);
            const double clave[2][3] = { { 0.0, 0.75, 1.5 }, { 2.5, 3.25, -1.0 } }; // the rim, three then two
            for (const auto& half : clave) for (double beat : half) if (beat >= 0.0) w.drum(beat, KitDrum::Snare, 0.3f);
            w.bass(0, 1.4, w.bassRoot, 0.85f);
            w.bass(1.5, 0.45, w.fifth(), 0.65f);
            w.bass(2, 1.4, w.bassRoot, 0.8f);
            w.bass(3.5, 0.45, w.fifth(), 0.65f);
            for (double beat : { 0.5, 1.5, 2.25, 3.0 }) w.keys(beat, 0.45, 0.35f);
        } },
        { "Shuffle", 90, 150, 4, false, 1.0f, 0.0f, [](BarWriter& w){
            for (int i = 0; i < 8; i++) w.drum(i * 0.5, KitDrum::Hat, i % 2 ? 0.3f : 0.55f);
            w.drum(0, KitDrum::Kick, 0.85f);
            w.drum(2, KitDrum::Kick, 0.8f);
            w.drum(1, KitDrum::Snare, 0.85f);
            if (w.phraseEnd()) snareFill(w, 3.0); else w.drum(3, KitDrum::Snare, 0.85f);
            // The boogie: up the chord and back down
            const int line[8] = { w.bassRoot, w.third(), w.fifth(), w.sixth(), w.seventh(), w.sixth(), w.fifth(), w.third() };
            for (int i = 0; i < 8; i++) w.bass(i * 0.5, 0.45, line[i], i % 2 ? 0.6f : 0.8f);
            w.keys(1, 0.45, 0.4f);
            w.keys(3, 0.45, 0.4f);
        } },
        { "Waltz", 60, 200, 3, false, 0.0f, 0.0f, [](BarWriter& w){
            w.drum(0, KitDrum::Kick, 0.8f);
            w.drum(1, KitDrum::Hat, 0.4f);
            w.drum(2, KitDrum::Hat, 0.4f);
            w.drum(1, KitDrum::Snare, 0.25f);
            w.drum(2, KitDrum::Snare, 0.25f);
            const int lowFifth = w.fifth() - 12 >= 28 ? w.fifth() - 12 : w.fifth(); // the fifth below the root, where it's in reach
            w.bass(0, 0.9, w.bar % 2 ? lowFifth : w.bassRoot, 0.85f);
            w.keys(1, 0.8, 0.35f);
            w.keys(2, 0.8, 0.35f);
        } },
        { "Groove", 40, 240, 0, false, 0.0f, 0.0f, [](BarWriter& w){ // any other meter: a beat at a time
            for (int i = 0; i < w.beats; i++){
                w.drum(i, i == 0 ? KitDrum::Kick : KitDrum::Hat, i == 0 ? 0.85f : 0.4f);
                w.drum(i + 0.5, KitDrum::Hat, 0.25f);
                w.bass(i, 0.9, i % 2 ? w.fifth() : w.bassRoot, i == 0 ? 0.85f : 0.65f);
            }
            w.keys(0, std::max(1.0, w.beats - 0.2), 0.35f);
        } },
    };
    return STYLES;
}

// A chord's notes for the keys, near the last chord's (the hand moving as little as it can), around middle C
std::vector<int> voiceChord(const BandChord& chord, bool seventh, const std::vector<int>& last){
    std::vector<int> classes = { chord.root, chord.root + (chord.minor ? 3 : 4), chord.root + 7 };
    if (seventh) classes.push_back(chord.root + (chord.minor ? 10 : 11));
    const double centre = last.empty() ? 60.0 : [&]{ double sum = 0; for (int p : last) sum += p; return sum / last.size(); }();
    std::vector<int> best;
    double bestCost = 1e9;
    // Each inversion: its lowest note placed so the chord sits between D3 and C5, the one nearest the last kept
    for (size_t inversion = 0; inversion < classes.size(); inversion++){
        std::vector<int> notes;
        for (size_t i = 0; i < classes.size(); i++) notes.push_back(classes[(inversion + i) % classes.size()] % 12);
        for (int base = 45; base <= 64; base++){
            if (base % 12 != notes[0]) continue;
            std::vector<int> voiced = { base };
            for (size_t i = 1; i < notes.size(); i++){
                int pitch = voiced.back() + 1;
                while (pitch % 12 != notes[i]) pitch++;
                voiced.push_back(pitch);
            }
            if (voiced.front() < 50 || voiced.back() > 72) continue;
            double sum = 0;
            for (int p : voiced) sum += p;
            const double cost = std::fabs(sum / voiced.size() - centre);
            if (cost < bestCost){
                bestCost = cost;
                best = voiced;
            }
        }
    }
    return best;
}

// The bass's note for a root: from E1 up to D#2, where a bass's lowest strings are
int bassPitch(int root){
    return 28 + ((root - 28) % 12 + 12) % 12;
}

} // namespace

int bandStyleCount(){ return (int)styles().size(); }

const char* bandStyleName(int style){
    return styles()[(size_t)std::clamp(style, 0, bandStyleCount() - 1)].name;
}

int bandStyleFor(unsigned seed, int tempo, int beatsPerBar){
    std::vector<int> fitting;
    const std::vector<Style>& all = styles();
    for (int i = 0; i < (int)all.size(); i++)
        if (all[i].beats == beatsPerBar && tempo >= all[i].lowTempo && tempo <= all[i].highTempo) fitting.push_back(i);
    if (fitting.empty()) // none for this tempo: any for these beats, else the one for any
        for (int i = 0; i < (int)all.size(); i++) if (all[i].beats == beatsPerBar) fitting.push_back(i);
    if (fitting.empty()) return (int)all.size() - 1;
    std::mt19937 random(seed * 2654435761u + 7u);
    return fitting[random() % fitting.size()];
}

std::vector<BandChord> harmonize(const std::vector<DrillNote>& notes, const KeySignature& key, int beatsPerBar, int bars, unsigned seed){
    std::vector<BandChord> out;
    if (bars <= 0) return out;
    const int tonic = tonicOf(key);
    const KeyChord* chords = key.minor ? MINOR_CHORDS : MAJOR_CHORDS;
    const int beats = std::max(1, beatsPerBar);
    std::mt19937 random(seed + 12345u);
    std::uniform_real_distribution<float> jitter(0.0f, 0.6f);

    // How each chord fits each bar's notes: strong beats and long notes count most
    std::vector<std::vector<float>> fit((size_t)bars, std::vector<float>(KEY_CHORDS, 0.0f));
    for (size_t n = 0; n < notes.size(); n++){
        const int bar = (int)std::floor(notes[n].beat / beats + 1e-9);
        if (bar < 0 || bar >= bars) continue;
        const double within = notes[n].beat - bar * beats;
        const double length = std::min(2.0, n + 1 < notes.size() ? notes[n + 1].beat - notes[n].beat : 1.0);
        const bool onBeat = std::fabs(within - std::round(within)) < 1e-6;
        const bool middle = beats % 2 == 0 && std::fabs(within - beats / 2) < 1e-6; // the bar's second strong beat
        const float weight = (float)((within < 1e-6 ? 3.0 : middle ? 1.5 : onBeat ? 1.0 : 0.5) * std::max(0.25, length));
        for (int c = 0; c < KEY_CHORDS; c++){
            const int root = (tonic + chords[c].offset) % 12;
            const int interval = ((notes[n].pitch - root) % 12 + 12) % 12;
            fit[(size_t)bar][(size_t)c] += weight * (chords[c].minor ? MINOR_FIT[interval] : MAJOR_FIT[interval]);
        }
    }
    // The best path through them (Viterbi): each bar's fit, a little chance, and how each chord follows the last;
    // home at the start and, firmly, at the end, the tension or the subdominant just before it
    std::vector<std::vector<float>> best((size_t)bars, std::vector<float>(KEY_CHORDS, -1e9f));
    std::vector<std::vector<int>> from((size_t)bars, std::vector<int>(KEY_CHORDS, 0));
    for (int bar = 0; bar < bars; bar++){
        for (int c = 0; c < KEY_CHORDS; c++){
            float own = fit[(size_t)bar][(size_t)c] + jitter(random);
            if (bar == 0 && c == 0) own += 1.5f;
            if (bar == bars - 1 && c == 0) own += 4.0f;
            if (bars > 2 && bar == bars - 2 && chords[c].function != 'T') own += 0.8f;
            if (bar == 0){
                best[0][(size_t)c] = own;
                continue;
            }
            for (int p = 0; p < KEY_CHORDS; p++){
                const float total = best[(size_t)bar - 1][(size_t)p] + moveScore(chords[p], chords[c], p == c) + own;
                if (total > best[(size_t)bar][(size_t)c]){
                    best[(size_t)bar][(size_t)c] = total;
                    from[(size_t)bar][(size_t)c] = p;
                }
            }
        }
    }
    int c = (int)(std::max_element(best.back().begin(), best.back().end()) - best.back().begin());
    std::vector<int> path((size_t)bars);
    for (int bar = bars - 1; bar >= 0; bar--){
        path[(size_t)bar] = c;
        c = from[(size_t)bar][(size_t)c];
    }
    for (int index : path) out.push_back({ (tonic + chords[index].offset) % 12, chords[index].minor });
    return out;
}

BandSong makeBandSong(const std::vector<DrillNote>& notes, const KeySignature& key, int beatsPerBar, int bars, int style, unsigned seed){
    BandSong song;
    const std::vector<Style>& all = styles();
    song.style = std::clamp(style, 0, (int)all.size() - 1);
    const Style& chosen = all[(size_t)song.style];
    const int beats = std::max(1, beatsPerBar);
    bars = std::max(1, bars);
    const std::vector<BandChord> chords = harmonize(notes, key, beats, bars, seed);
    const char* const* names = key.fifths < 0 ? FLAT_NAMES : SHARP_NAMES;
    for (const BandChord& chord : chords) song.chords.push_back(std::string(names[chord.root]) + (chord.minor ? "m" : ""));

    std::mt19937 random(seed + 777u);
    // The count-in: sticks on every beat of the bar before, the last a little louder
    for (int i = 0; i < beats; i++){
        BandHit stick;
        stick.part = BandPart::Drums;
        stick.beat = i - beats;
        stick.drum = KitDrum::Hat;
        stick.velocity = i == 0 ? 0.75f : 0.55f;
        song.hits.push_back(stick);
    }
    std::vector<int> voicing;
    for (int bar = 0; bar < bars; bar++){
        BarWriter w;
        w.hits = &song.hits;
        w.start = bar * beats;
        w.beats = beats;
        w.bar = bar;
        w.bars = bars;
        w.chord = chords[(size_t)bar];
        w.next = bar + 1 < bars ? chords[(size_t)bar + 1] : chords[0];
        w.bassRoot = bassPitch(w.chord.root);
        w.nextBassRoot = bassPitch(bar + 1 < bars ? w.next.root : chords.back().root);
        voicing = voiceChord(w.chord, chosen.sevenths && !(bar + 1 == bars), voicing);
        w.voicing = voicing;
        w.swing8 = chosen.swing8;
        w.swing16 = chosen.swing16;
        w.random = &random;
        if (bar % 4 == 0) w.drum(0, KitDrum::Crash, bar == 0 ? 0.7f : 0.45f); // each four bars marked
        chosen.write(w);
    }
    // The ending: the home chord, held, on the bar after the last, with a crash
    BarWriter end;
    end.hits = &song.hits;
    end.start = bars * beats;
    end.beats = beats;
    end.bar = bars;
    end.bars = bars + 1;
    end.chord = { (tonicOf(key)), key.minor };
    end.next = end.chord;
    end.bassRoot = bassPitch(end.chord.root);
    end.nextBassRoot = end.bassRoot;
    end.voicing = voiceChord(end.chord, false, voicing);
    end.random = &random;
    end.drum(0, KitDrum::Crash, 0.8f);
    end.drum(0, KitDrum::Kick, 0.9f);
    end.bass(0, 2.0, end.bassRoot, 0.9f);
    end.keys(0, 2.0, 0.45f);
    song.endBeat = bars * beats + 3.0;
    std::stable_sort(song.hits.begin(), song.hits.end(), [](const BandHit& a, const BandHit& b){ return a.beat < b.beat; });
    return song;
}
