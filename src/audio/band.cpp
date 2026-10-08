#include "audio/band.h"

#include "audio/audio.h"
#include "core/music.h"

#include <algorithm>

const float DRUMS_VOLUME = 0.45f, BASS_VOLUME = 0.55f, KEYS_VOLUME = 0.4f; // under the player: they're the lead
const float KIT_SECONDS[5] = { 0.5f, 0.35f, 0.12f, 0.5f, 2.0f };        // as KitDrum

BandPlayer::BandPlayer(){
    const int rate = std::max(1, audioSampleRate());
    for (int drum = 0; drum < 5; drum++){
        kit[drum].resize((size_t)(KIT_SECONDS[drum] * rate));
        renderKitDrum(kit[drum].data(), (int)kit[drum].size(), rate, (KitDrum)drum, 3);
    }
}

void BandPlayer::start(const BandSong& played, double firstDownbeat, double beatSeconds){
    song = played;
    downbeat = firstDownbeat;
    beat = beatSeconds;
    next = 0;
    playing = true;
}

void BandPlayer::schedule(double until){
    if (!playing) return;
    for (; next < song.hits.size(); next++){
        const BandHit& hit = song.hits[next];
        const double at = downbeat + hit.beat * beat;
        if (at >= until) break;
        const float seconds = (float)(hit.length * beat);
        if (loudness <= 0.0f) continue;
        switch (hit.part){
            case BandPart::Drums: playSamplesAt(kit[(int)hit.drum], at, DRUMS_VOLUME * hit.velocity * loudness); break;
            case BandPart::Bass: playStringNoteAt(midiToFrequency((float)hit.pitch), true, seconds, at, BASS_VOLUME * hit.velocity * loudness); break;
            case BandPart::Keys: playBuiltInNoteAt("keys", midiToFrequency((float)hit.pitch), at, seconds + 0.15f, KEYS_VOLUME * hit.velocity * loudness); break;
        }
    }
    if (next >= song.hits.size()) playing = false;
}

void BandPlayer::stop(){
    playing = false;
}
