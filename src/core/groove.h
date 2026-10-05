#pragma once

#include "core/synth.h"

#include <map>
#include <string>
#include <vector>

// A backing tune for the games: a few bars of guitar, bass and drums, each with a name, written in one key and played
// in any (every note moved by the same number of semitones); a phrase is those bars in an order. A text file, meant
// to be changed by hand (resources/games/*.groove):
//
//   tempo 138
//   key F#                         the key it's written in
//   phrase call riff riff end      a phrase's bars, in order
//   riff,call guitar 2.5 0.5 C#3   bar(s) part beat length note: the guitar or the bass (beats from 0)
//   call drums 0 crash             bar(s) drums beat drum: kick, snare, hat, open, crash
//
// '#' starts a comment. A bar can be named in several lines (riff,call: both bars have it).
enum class GroovePart { Guitar, Bass, Drums };

struct GrooveHit {
    GroovePart part;
    double beat;
    double length = 0.0; // beats; not for drums
    int pitch = 0;       // MIDI; not for drums
    KitDrum drum = KitDrum::Kick;
};

struct Groove {
    float tempo = 120.0f;
    int key = 0;      // a pitch class
    int barBeats = 4;
    std::vector<std::string> phrase;
    std::map<std::string, std::vector<GrooveHit>> bars;
};

bool parseGroove(const std::string& text, Groove& groove, std::string& error);
bool loadGroove(const std::string& path, Groove& groove, std::string& error);

// How far to move the tune to play it in `root` (a pitch class): the nearest way, from 2 semitones down to 9 up, so
// the bass stays where a bass reaches
int grooveShift(const Groove& groove, int root);
// A bar of the phrase (its index, going round), its hits moved to `root`
std::vector<GrooveHit> grooveBar(const Groove& groove, int barInPhrase, int root);
