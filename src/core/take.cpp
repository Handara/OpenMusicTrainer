#include "core/take.h"

#include <algorithm>
#include <cmath>

namespace {

int stepNear(const Chart& chart, int step, double seconds){
    return (int)std::lround(secondsToTick(chart, seconds) / step) * step;
}

// The track's note on that step of that string: notes are sorted by tick, then by string
std::vector<FrettedNote>::iterator noteAt(FrettedTrack& track, int tick, int stringIndex){
    return std::lower_bound(track.notes.begin(), track.notes.end(), 0, [&](const FrettedNote& note, int){
        return note.tick < tick || (note.tick == tick && note.stringIndex < stringIndex);
    });
}

// The notes still ringing last to `tick` (one step at least); `over`: and ring no more
void holdRinging(Take& take, FrettedTrack& track, int step, int tick, bool over){
    for (Take::Written& written : take.written){
        if (!written.ringing) continue;
        auto note = noteAt(track, written.tick, written.stringIndex);
        bool there = note != track.notes.end() && note->tick == written.tick && note->stringIndex == written.stringIndex;
        if (there) note->duration = std::max(step, tick - written.tick);
        if (over) written.ringing = false;
    }
}

} // namespace

void takePluck(Take& take, const Chart& chart, FrettedTrack& track, int step, const std::vector<int>& pitches, double seconds){
    step = std::max(1, step);
    const int tick = stepNear(chart, step, seconds);
    if (tick < 0) return;

    // The same pluck, written a moment ago as one note: these take its place
    StringFret hand = take.hand;
    for (size_t i = take.written.size(); i-- > 0;){
        const Take::Written written = take.written[i];
        if (std::fabs(written.seconds - seconds) >= TAKE_SAME_PLUCK_S) break; // they're in the order played
        auto note = noteAt(track, written.tick, written.stringIndex);
        if (note != track.notes.end() && note->tick == written.tick && note->stringIndex == written.stringIndex) track.notes.erase(note);
        take.written.erase(take.written.begin() + i);
    }

    std::vector<StringFret> places = chordPositions(pitches, track.tuning, MAX_FRET, hand);
    if (std::none_of(places.begin(), places.end(), [](const StringFret& place){ return place.string >= 0; })) return;
    holdRinging(take, track, step, tick, true);
    for (const StringFret& place : places){
        if (place.string < 0) continue; // off this neck
        auto note = noteAt(track, tick, place.string);
        if (note != track.notes.end() && note->tick == tick && note->stringIndex == place.string){
            note->fret = place.fret;
            note->duration = step;
        } else {
            track.notes.insert(note, FrettedNote{ tick, place.string, place.fret, step });
        }
        take.written.push_back({ tick, place.string, seconds, true });
    }
    for (const StringFret& place : places) if (place.string >= 0){ take.hand = place; break; } // the lowest note's place
    take.loudestDb = -120.0f;
}

void takeLevel(Take& take, const Chart& chart, FrettedTrack& track, int step, float levelDb, double seconds){
    step = std::max(1, step);
    take.loudestDb = std::max(take.loudestDb, levelDb);
    bool died = levelDb < TAKE_SILENCE_DB || levelDb < take.loudestDb - TAKE_DIES_DB;
    holdRinging(take, track, step, stepNear(chart, step, seconds), died);
}

void endTake(Take& take, const Chart& chart, FrettedTrack& track, int step, double seconds){
    step = std::max(1, step);
    holdRinging(take, track, step, stepNear(chart, step, seconds), true);
}
