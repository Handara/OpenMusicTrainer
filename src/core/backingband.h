#pragma once

#include "core/drill.h"
#include "core/notation.h"
#include "core/synth.h"

#include <string>
#include <vector>

// A backing band for the timed drills, so practice sounds like playing in a song rather than to a click: drums, bass
// and an electric piano in a style (rock, funk, reggae, disco, hip-hop, bossa, shuffle; a waltz in 3/4), playing
// chords chosen to fit the notes the player reads. Each bar's chord is one of the key's own, the one its notes sit
// best in, strong beats and long notes counting most; together they make a progression that moves the way songs do
// (to the subdominant, the dominant, home) and comes home at the end. The player is the melody: the band never plays
// it. Pure: what to play and when; the drill schedules it (audio/band).

enum class BandPart { Drums, Bass, Keys };

struct BandHit {
    BandPart part = BandPart::Drums;
    double beat = 0.0;     // from the first bar's downbeat; the count-in's are before it (negative)
    double length = 0.0;   // beats (not for drums)
    int pitch = 0;         // MIDI (not for drums)
    KitDrum drum = KitDrum::Kick;
    float velocity = 1.0f; // 0 to 1
};

struct BandSong {
    int style = 0;
    std::vector<std::string> chords; // each bar's, named as they're written ("G", "Em", "Bb")
    std::vector<BandHit> hits;       // in time order: a count-in bar, the bars, an ending on the bar after the last
    double endBeat = 0.0;            // the ending's last sound is over by then
};

int bandStyleCount();
const char* bandStyleName(int style); // "Funk"
// A style that suits the tempo and the bar's beats (a waltz for three), picked by `seed` among them: a lesson's
// own seed gives it its own style, the same every time
int bandStyleFor(unsigned seed, int tempo, int beatsPerBar);

// The band for a pass: its notes (beats from the first downbeat), in its key, `bars` bars of `beatsPerBar` beats.
// `seed` varies what the notes leave open (which of the chords that fit, the fills), so a pass's song is its own.
BandSong makeBandSong(const std::vector<DrillNote>& notes, const KeySignature& key, int beatsPerBar, int bars, int style, unsigned seed);

// The chords alone, each bar's as its root (a pitch class) and whether it's minor: for the tests, and makeBandSong
struct BandChord {
    int root = 0;
    bool minor = false;
};
std::vector<BandChord> harmonize(const std::vector<DrillNote>& notes, const KeySignature& key, int beatsPerBar, int bars, unsigned seed);
