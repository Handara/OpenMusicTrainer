#pragma once

#include "core/backingband.h"

#include <vector>

// A backing band's song (core/backingband) played on lahn's own sounds: its drum kit, a fingered bass, an electric
// piano. Its hits are handed to the audio engine a moment before their time, as a metronome's clicks are, so a voice
// is only taken when it's about to sound.
class BandPlayer {
public:
    BandPlayer(); // its kit, rendered once
    // The song from now on: its first bar's downbeat at `downbeat` on the audio clock (audioTime), a beat this long.
    // Its count-in comes the bar before.
    void start(const BandSong& song, double downbeat, double beatSeconds);
    void schedule(double until); // every hit before `until` (audio time) handed to the engine
    void stop();                 // nothing more handed (what's handed still plays: stopPreviews for that)
    bool active() const { return playing; }
    double endTime() const { return downbeat + song.endBeat * beat; }

private:
    BandSong song;
    double downbeat = 0.0, beat = 0.5;
    size_t next = 0;
    bool playing = false;
    std::vector<float> kit[5];
};
