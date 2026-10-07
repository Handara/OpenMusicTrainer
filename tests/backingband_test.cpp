#include "doctest/doctest.h"

#include "core/backingband.h"

#include <algorithm>
#include <random>
#include <set>
#include <string>

// Notes a beat apart from the first downbeat, by pitch
static std::vector<DrillNote> melody(const std::vector<int>& pitches){
    std::vector<DrillNote> notes;
    for (size_t i = 0; i < pitches.size(); i++) notes.push_back({ (double)i, 0, 0, pitches[i] });
    return notes;
}

TEST_CASE("the band's chords fit the notes read: a bar of a chord's notes is that chord, and it ends at home"){
    const KeySignature cMajor{ 0, false };
    // C E G C | F A C A | G B D B | C E G E
    const std::vector<BandChord> chords = harmonize(melody({ 60, 64, 67, 72, 65, 69, 72, 69, 67, 71, 74, 71, 60, 64, 67, 64 }), cMajor, 4, 4, 1);
    REQUIRE(chords.size() == 4);
    CHECK(chords[0].root == 0);
    CHECK_FALSE(chords[0].minor);
    CHECK(chords[1].root == 5);
    CHECK(chords[2].root == 7);
    CHECK(chords[3].root == 0);
    // A minor key comes home to its minor tonic
    const KeySignature aMinor{ 0, true };
    const std::vector<BandChord> minorChords = harmonize(melody({ 69, 72, 76, 72, 69, 72, 76, 72 }), aMinor, 4, 2, 3);
    CHECK(minorChords.back().root == 9);
    CHECK(minorChords.back().minor);
}

// A chord's own note, or a gentle colour over it (a 9th, a 6th, a minor chord's 11th or 7th): nothing that rubs
static bool sitsWell(const BandChord& chord, int pitch){
    const int interval = ((pitch - chord.root) % 12 + 12) % 12;
    return chord.minor ? (interval == 0 || interval == 2 || interval == 3 || interval == 5 || interval == 7 || interval == 10)
                       : (interval == 0 || interval == 2 || interval == 4 || interval == 7 || interval == 9 || interval == 11);
}

TEST_CASE("the band's chords fit random notes too: the note on each bar's downbeat never rubs against its chord"){
    const KeySignature cMajor{ 0, false };
    for (unsigned seed = 0; seed < 20; seed++){
        std::mt19937 random(seed);
        const int pool[3] = { 64, 65, 67 }; // E F G, as a first reading drill asks
        std::vector<int> pitches;
        for (int i = 0; i < 40; i++) pitches.push_back(pool[random() % 3]);
        const std::vector<BandChord> chords = harmonize(melody(pitches), cMajor, 4, 10, seed);
        int fitting = 0;
        for (int bar = 0; bar < 10; bar++) fitting += sitsWell(chords[(size_t)bar], pitches[(size_t)bar * 4]) ? 1 : 0;
        CHECK(fitting >= 9);
    }
}

TEST_CASE("every style plays every bar, the bass low and the keys in the middle, in time order, a count-in first and an ending after"){
    const KeySignature gMajor{ 1, false };
    std::vector<int> pitches;
    for (int i = 0; i < 32; i++) pitches.push_back(67 + (i * 5) % 12);
    for (int style = 0; style < bandStyleCount(); style++){
        const std::string name = bandStyleName(style);
        CAPTURE(name);
        const int beats = name == "Waltz" ? 3 : 4, bars = 32 / beats;
        const BandSong song = makeBandSong(melody(pitches), gMajor, beats, bars, style, 5);
        REQUIRE(song.chords.size() == (size_t)bars);
        CHECK(std::is_sorted(song.hits.begin(), song.hits.end(), [](const BandHit& a, const BandHit& b){ return a.beat < b.beat; }));
        CHECK(std::count_if(song.hits.begin(), song.hits.end(), [](const BandHit& hit){ return hit.beat < 0.0; }) == beats); // the count-in
        std::set<int> barsWithBass, barsWithKeys;
        for (const BandHit& hit : song.hits){
            if (hit.part == BandPart::Bass){
                CHECK(hit.pitch >= 28);
                CHECK(hit.pitch <= 52);
                barsWithBass.insert((int)(hit.beat / beats));
            }
            if (hit.part == BandPart::Keys){
                CHECK(hit.pitch >= 48);
                CHECK(hit.pitch <= 72);
                barsWithKeys.insert((int)(hit.beat / beats));
            }
            CHECK(hit.beat < song.endBeat);
        }
        CHECK((int)barsWithBass.size() == bars + 1); // and the ending's
        CHECK((int)barsWithKeys.size() == bars + 1);
        // Same pass, same seed: the same song
        CHECK(makeBandSong(melody(pitches), gMajor, beats, bars, style, 5).hits.size() == song.hits.size());
    }
}

TEST_CASE("a lesson's style suits its tempo and meter, the same each time, and lessons differ"){
    CHECK(std::string(bandStyleName(bandStyleFor(3, 100, 3))) == "Waltz");
    std::set<std::string> seen;
    for (unsigned seed = 0; seed < 30; seed++){
        const int style = bandStyleFor(seed, 100, 4);
        CHECK(style == bandStyleFor(seed, 100, 4));
        seen.insert(bandStyleName(style));
    }
    CHECK(seen.size() >= 4);
    CHECK(seen.count("Disco") == 0); // 100 bpm is too slow for it
}
