#include "core/score.h"

#include <algorithm>
#include <climits>

int noteValueTicks(NoteValue value, int resolution){
    switch (value){
        case NoteValue::Whole:        return resolution * 4;
        case NoteValue::Half:         return resolution * 2;
        case NoteValue::Quarter:      return resolution;
        case NoteValue::Eighth:       return resolution / 2;
        case NoteValue::Sixteenth:    return resolution / 4;
        case NoteValue::ThirtySecond: return resolution / 8;
    }
    return resolution;
}

int beamCount(NoteValue value){
    switch (value){
        case NoteValue::Eighth:       return 1;
        case NoteValue::Sixteenth:    return 2;
        case NoteValue::ThirtySecond: return 3;
        default:                      return 0;
    }
}

namespace {

struct Meter {
    int barLength;
    int beat;      // what the rules count in: a quarter in 4/4, a half in 2/2, a dotted quarter in 6/8
    bool compound; // 6/8, 9/8, 12/8: beats of three eighths
};

Meter meterOf(const Chart& chart, const TimeSignatureChange& time){
    Meter meter;
    meter.barLength = ticksPerBar(chart, time);
    meter.compound = time.beatUnit == 8 && time.beats % 3 == 0 && time.beats > 3;
    meter.beat = meter.compound ? chart.resolution * 3 / 2 : chart.resolution * 4 / time.beatUnit;
    return meter;
}

// A written value that a length can be: a note value, maybe dotted
struct Candidate {
    NoteValue value;
    int dots;
    int length;
};

// Every plain and dotted value, longest first. Values the resolution can't express exactly are left out.
std::vector<Candidate> binaryCandidates(int resolution){
    std::vector<Candidate> candidates;
    for (int v = (int)NoteValue::Whole; v <= (int)NoteValue::ThirtySecond; v++){
        NoteValue value = (NoteValue)v;
        int length = noteValueTicks(value, resolution);
        if (length <= 0) continue;
        if (value != NoteValue::ThirtySecond && length % 2 == 0) candidates.push_back({value, 1, length * 3 / 2});
        candidates.push_back({value, 0, length});
    }
    std::sort(candidates.begin(), candidates.end(), [](const Candidate& a, const Candidate& b){ return a.length > b.length; });
    return candidates;
}

// Charts may have notes a little off the grid (recorded, or imported): for writing them down, each tick snaps to
// the nearest 32nd note, unless it's exactly on a triplet sixteenth
int quantize(int tick, int resolution){
    int binary = resolution / 8, triplet = resolution / 6;
    if (binary <= 0 || tick % binary == 0) return tick;
    if (triplet > 0 && resolution % 6 == 0 && tick % triplet == 0) return tick;
    return (tick + binary / 2) / binary * binary;
}

// The rules for one written value at a place in the bar (see score.h)
bool allowed(int positionInBar, const Candidate& candidate, bool rest, const Meter& meter){
    int length = candidate.length;
    if (rest && candidate.dots > 0 && !meter.compound) return false;
    // A rest longer than a beat only fills half the bar, from its start or its middle: a half rest in 4/4, never in 3/4
    if (rest && length > meter.beat && (length * 2 != meter.barLength || positionInBar % length != 0)) return false;
    if (length <= meter.beat) return positionInBar / meter.beat == (positionInBar + length - 1) / meter.beat; // within one beat
    if (positionInBar % meter.beat != 0) return false;                                                       // starts on a beat
    return !meter.compound || length % meter.beat == 0;
}

struct Chord {
    int start, end;  // as written
    int sustain;     // the longest of its notes' lengths, 0 if none is given
    int firstNote, count;
};

struct Span {
    int start, end;
    int chord;         // index into the chords, -1 for a rest
    bool continuesAfter; // the chord goes on past the end of this span (into the next bar)
};

} // namespace

Score buildScore(const Chart& chart, const FrettedTrack& track){
    Score score;
    const int resolution = chart.resolution;
    score.resolution = resolution;

    // Bars: every one that starts before the end, plus the line that closes the last
    int barCount = 0;
    while (barStartTick(chart, barCount) < chart.endTick) barCount++;
    for (int b = 0; b <= barCount; b++){
        ScoreBar bar{};
        bar.tick = barStartTick(chart, b);
        bar.time = (float)tickToSeconds(chart, bar.tick);
        bar.timeSignature = timeSignatureAt(chart, bar.tick);
        for (const KeyChange& change : chart.keys) if (change.tick <= bar.tick) bar.key = change.key;
        const ScoreBar* before = b > 0 ? &score.bars[b - 1] : nullptr;
        bar.showTimeSignature = !before || before->timeSignature.beats != bar.timeSignature.beats
                                        || before->timeSignature.beatUnit != bar.timeSignature.beatUnit;
        bar.showKey = !before || before->key.fifths != bar.key.fifths;
        score.bars.push_back(bar);
    }

    // Chords: notes at the same tick, and how long each is written
    std::vector<Chord> chords;
    for (int i = 0; i < (int)track.notes.size();){
        int j = i;
        int sustain = 0;
        while (j < (int)track.notes.size() && track.notes[j].tick == track.notes[i].tick) sustain = std::max(sustain, track.notes[j++].duration);
        chords.push_back({quantize(track.notes[i].tick, resolution), 0, sustain, i, j - i}); // end is worked out below
        i = j;
    }
    for (size_t c = 0; c < chords.size(); c++){
        Chord& chord = chords[c];
        int next = c + 1 < chords.size() ? chords[c + 1].start : INT_MAX;
        int barEnd = barStartTick(chart, barNumberAt(chart, chord.start) + 1);
        chord.end = chord.sustain > 0 ? std::min(quantize(chord.start + chord.sustain, resolution), next) : std::min(next, barEnd);
    }

    const std::vector<Candidate> candidates = binaryCandidates(resolution);
    int nextBeamGroup = 0;
    size_t firstChord = 0; // the first chord that may still reach into the current bar
    for (int b = 0; b < barCount; b++){
        const int barStart = score.bars[b].tick, barEnd = score.bars[b + 1].tick;
        const Meter meter = meterOf(chart, score.bars[b].timeSignature);
        size_t eventsBefore = score.events.size();

        // The bar as spans of notes and rests, end to end
        std::vector<Span> spans;
        int cursor = barStart;
        while (firstChord < chords.size() && chords[firstChord].end <= barStart) firstChord++;
        for (size_t c = firstChord; c < chords.size() && chords[c].start < barEnd; c++){
            const Chord& chord = chords[c];
            if (chord.end <= chord.start) continue; // two chords quantized onto the same tick: the later one is written
            int start = std::max(chord.start, barStart), end = std::min(chord.end, barEnd);
            if (start > cursor) spans.push_back({cursor, start, -1, false});
            spans.push_back({start, end, (int)c, chord.end > barEnd});
            cursor = end;
        }
        if (cursor < barEnd) spans.push_back({cursor, barEnd, -1, false});

        if (spans.size() == 1 && spans[0].chord < 0){
            ScoreEvent rest{};
            rest.tick = barStart;
            rest.length = barEnd - barStart;
            rest.time = (float)tickToSeconds(chart, barStart);
            rest.value = NoteValue::Whole;
            rest.rest = rest.wholeBarRest = true;
            rest.firstNote = -1;
            rest.beamGroup = -1;
            rest.bar = b;
            score.events.push_back(rest);
            continue;
        }

        // Triplet beats: a quarter with a span boundary inside it that isn't on the 32nd grid. Its notes are counted
        // in thirds of the beat (triplet eighths), or sixths if they need them (triplet sixteenths).
        auto tripletUnit = [&](int quarterStart) -> int {
            if (meter.compound || resolution % 6 != 0 || resolution % 8 != 0) return 0;
            bool triplet = false, sixths = false;
            for (const Span& span : spans){
                for (int boundary : {span.start, span.end}){
                    if (boundary <= quarterStart || boundary >= quarterStart + resolution) continue;
                    if (boundary % (resolution / 8) != 0) triplet = true;
                    if ((boundary - quarterStart) % (resolution / 3) != 0) sixths = true;
                }
            }
            return triplet ? (sixths ? resolution / 6 : resolution / 3) : 0;
        };

        auto emit = [&](int tick, int length, NoteValue value, int dots, int tuplet, const Span& span, bool tied){
            ScoreEvent event{};
            event.tick = tick;
            event.length = length;
            event.time = (float)tickToSeconds(chart, tick);
            event.value = value;
            event.dots = dots;
            event.tuplet = tuplet;
            event.rest = span.chord < 0;
            event.tiedToNext = !event.rest && tied;
            event.firstNote = event.rest ? -1 : chords[span.chord].firstNote;
            event.noteCount = event.rest ? 0 : chords[span.chord].count;
            event.beamGroup = -1;
            event.bar = b;
            score.events.push_back(event);
        };

        for (const Span& span : spans){
            for (int p = span.start; p < span.end;){
                // The piece ends where the span does, or at the edge of a triplet beat, whichever comes first
                int quarter = barStart + (p - barStart) / resolution * resolution;
                int unit = tripletUnit(quarter);
                int pieceEnd = span.end;
                if (unit > 0) pieceEnd = std::min(pieceEnd, quarter + resolution);
                else for (int q = quarter + resolution; q < pieceEnd; q += resolution) if (tripletUnit(q) > 0){ pieceEnd = q; break; }

                while (p < pieceEnd){
                    if (unit > 0){
                        // In a triplet beat: the biggest written value that fits, counted in triplet units
                        int units = (pieceEnd - p) / unit;
                        NoteValue value = NoteValue::ThirtySecond;
                        int dots = 0, used = units;
                        if (unit == resolution / 3){
                            used = std::min(units, 2);
                            value = used == 2 ? NoteValue::Quarter : NoteValue::Eighth;
                        } else {
                            used = std::min(units, 4);
                            if (used == 4) value = NoteValue::Quarter;
                            else if (used == 3){ value = NoteValue::Eighth; dots = 1; }
                            else if (used == 2) value = NoteValue::Eighth;
                            else value = NoteValue::Sixteenth;
                        }
                        int length = used > 0 ? used * unit : pieceEnd - p;
                        emit(p, length, value, dots, 3, span, p + length < span.end || span.continuesAfter);
                        p += length;
                    } else {
                        const Candidate* chosen = nullptr;
                        for (const Candidate& candidate : candidates){
                            if (candidate.length <= pieceEnd - p && allowed(p - barStart, candidate, span.chord < 0, meter)){
                                chosen = &candidate;
                                break;
                            }
                        }
                        Candidate fallback = {NoteValue::ThirtySecond, 0, pieceEnd - p}; // only off-grid leftovers get here
                        if (!chosen) chosen = &fallback;
                        emit(p, chosen->length, chosen->value, chosen->dots, 0, span, p + chosen->length < span.end || span.continuesAfter);
                        p += chosen->length;
                    }
                }
            }
        }

        // Beams: runs of eighths and shorter, with no rest between them, inside one beat
        int beamUnit = meter.compound ? meter.beat : resolution;
        int groupStart = -1;
        auto closeGroup = [&](size_t end){
            if (groupStart >= 0 && (int)end - groupStart >= 2){
                for (size_t e = groupStart; e < end; e++) score.events[e].beamGroup = nextBeamGroup;
                nextBeamGroup++;
            }
            groupStart = -1;
        };
        for (size_t e = eventsBefore; e < score.events.size(); e++){
            const ScoreEvent& event = score.events[e];
            bool beamable = !event.rest && beamCount(event.value) > 0;
            bool sameBeat = groupStart >= 0 && (event.tick - barStart) / beamUnit == (score.events[groupStart].tick - barStart) / beamUnit;
            if (!beamable || !sameBeat) closeGroup(e);
            if (beamable && groupStart < 0) groupStart = (int)e;
        }
        closeGroup(score.events.size());
    }
    return score;
}
