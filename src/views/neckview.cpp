#include "views/neckview.h"

#include "core/music.h"
#include "core/positions.h"
#include "views/smooth.h"
#include "views/viewfont.h"

#include <algorithm>
#include <cmath>

const int MIN_FRETS = 7;                // the neck shown is never narrower: fewer frets would look like a zoom
const int LAST_FRET = 24;
const int GUITAR_FRETS = 22, BASS_FRETS = 20; // the whole neck, as the Instrument screen shows it
const int BASS_BELOW = 40;              // an instrument whose lowest string is under E2 is a bass
const float MAX_STRING_SPACING = 64.0f; // at a 720-pixel-tall window: past it, strings only drift apart
const float OPEN_COLUMN_SHARE = 0.9f;   // the open strings' column, against a fret's
// A ring closes in the time a note takes to travel this far at the note speed: 1.2 s at the default 300 px/s, so a
// faster setting reads faster in every view
const float APPROACH_DISTANCE = 360.0f;
const float MIN_APPROACH_S = 0.45f;
const float MAX_APPROACH_S = 2.0f;
const float RING_START = 3.2f;          // a ring starts this many times its note's size
const float FADE_IN_SHARE = 0.25f;      // a note fades in over the first quarter of its ring's time
const float MISS_FADE_S = 0.5f;
const int SINGLE_DOTS[] = { 3, 5, 7, 9, 15, 17, 19, 21 }; // fret markers; 12 and 24 get two
const float WHOLE_NOTE_BEATS = 4.0f;    // one loop of a slider: a whole note (a bar of 4/4); a quarter is a quarter loop
const float MIN_SLIDER_BEATS = 0.75f;   // shorter than a dotted eighth: a plain hit, no slider
const float SLIDER_WIDTH = 3.5f;        // at a 720-pixel-tall window
const float TRACK_ALPHA = 0.35f;        // a slider's track before it's played
const float HELD_FLASH_S = 0.15f;       // a held note's solid flash at the hit, before it goes hollow

// An arc on a note's rim, clockwise from the top, `from` to `to` in loops (0 to 1)
static void drawRimArc(Vector2 at, float rim, float from, float to, float width, Color color){
    if (to <= from) return;
    smoothRing(at, rim - width / 2, rim + width / 2, -90.0f + 360.0f * from, -90.0f + 360.0f * to, color);
}

// What the song needs, worked out once per song rather than every frame: the part of the neck, and how long its
// longest note is held (a held note stays drawn until it's over, so the notes drawn reach back that far)
struct SongShape {
    FretSpan span = { 0, MIN_FRETS };
    int highest = 0; // fret
    float longest = 0.0f;
};

static const SongShape& shapeOf(const std::vector<PlayNote>& notes){
    static const PlayNote* data = nullptr;
    static size_t size = 0;
    static float first = 0.0f, last = 0.0f;
    static SongShape shape;
    bool same = notes.data() == data && notes.size() == size &&
                (notes.empty() || (notes.front().time == first && notes.back().time == last));
    if (!same){
        std::vector<int> frets;
        frets.reserve(notes.size());
        shape.longest = 0.0f;
        for (const PlayNote& note : notes){
            frets.push_back(note.fret);
            shape.longest = std::max(shape.longest, note.writtenLength);
        }
        shape.span = fretSpanFor(frets, MIN_FRETS, LAST_FRET);
        shape.highest = frets.empty() ? 0 : *std::max_element(frets.begin(), frets.end());
        data = notes.data();
        size = notes.size();
        first = notes.empty() ? 0.0f : notes.front().time;
        last = notes.empty() ? 0.0f : notes.back().time;
    }
    return shape;
}

// Depth: a note to play drops onto its place from above the board, at a steady speed: how high it is says how soon
// it's due. Notes repeated on one spot then come down as a column, one after the other, where circles drawn in place
// would hide each other. While it falls its place is marked on the board (as a piece's is in Tetris), under its
// shadow, which gathers as it comes down, and a thread ties it to that place; it lands as its ring closes, with a
// small squash. A note higher up is nearer the eye: it's drawn over the ones under it, and where it covers one
// that's due before it, it's seen through, so the next one to play always reads. The board stays flat and
// square to the eye, so every fret and label reads at a glance; only the notes stand out from it.
const float MIN_DROP = 1.5f, MAX_DROP = 3.0f; // how far a note falls, in string spacings: from the view's top if that's between
const float DROP_GROW = 0.1f;      // higher is nearer the eye: that much bigger at the top of its fall
const float NOTE_DEPTH = 3.0f;     // at a 720-pixel-tall window: a note's side, showing under its face
const float LAND_S = 0.12f;        // the squash as it lands
const float RING_FROM = 0.55f;     // a note's ring appears when this much of its fall is left
const float COVERING_ALPHA = 0.4f; // how solid a note is while it's right over one due before it: that one shows through
const float COVERS_FULLY = 1.0f;   // it's that see-through once they overlap by this much, in note sizes

// The layout the neck was last drawn with, so where a note is can be asked afterwards (neckNoteAt)
static struct {
    double drawnAt = -100.0; // GetTime
    float boardLeft = 0, nut = 0, fretWidth = 0, top = 0, spacing = 0, radius = 0;
    int strings = 0, firstFretted = 1;
    bool lowStringOnTop = false;
} lastNeck;

bool neckNoteAt(const PlayNote& note, float& x, float& y, float& radius){
    if (GetTime() - lastNeck.drawnAt > 0.25) return false; // not on screen now
    x = note.fret == 0 ? lastNeck.boardLeft + lastNeck.fretWidth * OPEN_COLUMN_SHARE / 2
                       : lastNeck.nut + (note.fret - lastNeck.firstFretted + 0.5f) * lastNeck.fretWidth;
    y = lastNeck.top + lastNeck.spacing * (0.5f + (lastNeck.lowStringOnTop ? note.stringIndex : lastNeck.strings - 1 - note.stringIndex));
    radius = lastNeck.radius;
    return true;
}

void drawNeckView(Rectangle area, const std::vector<PlayNote>& notes, const std::vector<int>& tuning, bool lowStringOnTop,
                  bool wholeNeck, NoteLabel label, const TimeAxis& axis){
    const int strings = (int)tuning.size();
    if (strings == 0) return;
    const float s = GetScreenHeight() / 720.0f;
    const SongShape& shape = shapeOf(notes);
    FretSpan span = shape.span;
    if (wholeNeck){
        bool bass = *std::min_element(tuning.begin(), tuning.end()) < BASS_BELOW;
        span = { 0, std::min(LAST_FRET, std::max(bass ? BASS_FRETS : GUITAR_FRETS, shape.highest)) };
    }
    const bool open = span.first == 0;
    const int firstFretted = std::max(span.first, 1);
    const int fretted = span.last - firstFretted + 1;

    // The board: string names on its left, fret numbers under it, centered in the area
    const float pad = 14.0f * s, labelWidth = 34.0f * s, numbersHeight = 22.0f * s;
    const float boardLeft = area.x + pad + labelWidth, boardRight = area.x + area.width - pad;
    const float fretWidth = (boardRight - boardLeft) / (fretted + (open ? OPEN_COLUMN_SHARE : 0.0f));
    const float nut = boardLeft + (open ? fretWidth * OPEN_COLUMN_SHARE : 0.0f);
    const float spacing = std::min(MAX_STRING_SPACING * s, (area.height - 2 * pad - numbersHeight) / strings);
    const float boardHeight = spacing * strings;
    const float top = area.y + (area.height - boardHeight - numbersHeight) / 2;
    const float radius = std::min(fretWidth, spacing) * 0.36f;
    lastNeck = { GetTime(), boardLeft, nut, fretWidth, top, spacing, radius, strings, firstFretted, lowStringOnTop };
    auto stringY = [&](int string){ return top + spacing * (0.5f + (lowStringOnTop ? string : strings - 1 - string)); };
    auto fretX = [&](int fret){
        if (fret == 0) return boardLeft + fretWidth * OPEN_COLUMN_SHARE / 2;
        return nut + (fret - firstFretted + 0.5f) * fretWidth;
    };

    BeginScissorMode((int)area.x, (int)area.y, (int)area.width, (int)area.height);
    smoothRoundedRect({ boardLeft - 6 * s, top, boardRight - boardLeft + 12 * s, boardHeight }, 10 * s, themeColor(UiColor::Card));
    const float middle = top + boardHeight / 2;
    const Color line = themeColor(UiColor::StaffLine);
    for (int fret = firstFretted; fret <= span.last; fret++){
        if (std::count(std::begin(SINGLE_DOTS), std::end(SINGLE_DOTS), fret)) smoothCircle({ fretX(fret), middle }, 5 * s, line);
        if (fret == 12 || fret == 24){
            smoothCircle({ fretX(fret), middle - spacing }, 5 * s, line);
            smoothCircle({ fretX(fret), middle + spacing }, 5 * s, line);
        }
        float wire = std::round(nut + (fret - firstFretted + 1) * fretWidth);
        if (fret < span.last) DrawRectangleRec({ wire - 0.75f * s, top + 4 * s, 1.5f * s, boardHeight - 8 * s }, Fade(themeColor(UiColor::Dim), 0.5f));
        drawViewText(TextFormat("%d", fret), fretX(fret), top + boardHeight + numbersHeight / 2 + 2 * s, 13 * s, themeColor(UiColor::Dim));
    }
    // The nut, thick, when the open strings are shown; else the first shown fret's wire
    DrawRectangleRec({ nut - (open ? 2.0f : 0.75f) * s, top + 4 * s, (open ? 4.0f : 1.5f) * s, boardHeight - 8 * s },
                     open ? themeColor(UiColor::Ink) : Fade(themeColor(UiColor::Dim), 0.5f));
    for (int string = 0; string < strings; string++){
        float y = stringY(string), thickness = (1.0f + 0.3f * (strings - 1 - string)) * s; // lower strings are thicker
        DrawRectangleRec({ boardLeft, y - thickness / 2, boardRight - boardLeft, thickness }, Fade(themeColor(UiColor::Ink), 0.45f));
        drawViewText(pitchClassName(tuning[string]), area.x + pad + labelWidth / 2, y, 15 * s, themeColor(UiColor::Dim));
    }

    // The notes that show now: from those fading after a miss to those whose rings are just appearing
    const float now = axis.songTime;
    const float approach = std::clamp(APPROACH_DISTANCE / std::max(1.0f, axis.noteSpeed), MIN_APPROACH_S, MAX_APPROACH_S);
    auto from = std::lower_bound(notes.begin(), notes.end(), now - std::max(MISS_FADE_S + 0.2f, shape.longest),
                                 [](const PlayNote& note, float time){ return note.time < time; });
    auto to = std::upper_bound(from, notes.end(), now + approach,
                               [](float time, const PlayNote& note){ return time < note.time; });
    auto centerOf = [&](const PlayNote& note){ return Vector2{ fretX(note.fret), stringY(note.stringIndex) }; };
    auto alphaOf = [&](const PlayNote& note){ return std::clamp((1.0f - (note.time - now) / approach) / FADE_IN_SHARE, 0.0f, 1.0f); };

    // The hand's path: each coming note joined to the next one, faintly, from rim to rim
    const PlayNote* previous = nullptr;
    for (auto it = from; it != to; ++it){
        if (it->judged) continue;
        if (previous && it->time > previous->time){
            Vector2 a = centerOf(*previous), b = centerOf(*it);
            float dx = b.x - a.x, dy = b.y - a.y, length = std::sqrt(dx * dx + dy * dy);
            if (length > 2.5f * radius){
                Vector2 step = { dx / length * radius * 1.2f, dy / length * radius * 1.2f };
                float alpha = 0.4f * std::min(alphaOf(*previous), alphaOf(*it));
                smoothLine({ a.x + step.x, a.y + step.y }, { b.x - step.x, b.y - step.y }, 2.0f * s, Fade(themeColor(UiColor::Dim), alpha));
            }
        }
        previous = &*it;
    }

    // Held notes first, under everything: only their slider is left, so a note due next on the same fret shows
    // through. After the hit's flash the note itself is gone (it's not one to play any more, and it mustn't hide
    // one that is); the slider's ball eats its track, greying and fading, until the note is over.
    const float rim = radius + 4.5f * s;
    auto isSlider = [](const PlayNote& note){ return note.beats >= MIN_SLIDER_BEATS && note.writtenLength > 0.0f; };
    for (auto it = from; it != to; ++it){
        const PlayNote& note = *it;
        if (!note.hit || !isSlider(note) || now >= note.time + note.writtenLength) continue;
        Vector2 at = centerOf(note);
        const float share = std::min(1.0f, note.beats / WHOLE_NOTE_BEATS);
        Color lit = themeColor(note.wasPerfect ? UiColor::Good : UiColor::Accent);
        float held = std::max(0.0f, now - note.time);
        float progress = std::clamp(held / note.writtenLength, 0.0f, 1.0f);
        float flash = std::max(0.0f, 1.0f - held / HELD_FLASH_S);
        float fade = 1.0f - 0.6f * progress;
        Color grey = ColorLerp(lit, themeColor(UiColor::Dim), 0.3f + 0.7f * progress);
        if (flash > 0.0f) smoothCircle(at, radius * (0.6f + 0.4f * flash), Fade(lit, flash));
        float played = share * progress;
        drawRimArc(at, rim, played, share, SLIDER_WIDTH * s, Fade(grey, 0.85f * fade));
        float angle = (-90.0f + 360.0f * played) * DEG2RAD;
        smoothCircle({ at.x + rim * std::cos(angle), at.y + rim * std::sin(angle) }, SLIDER_WIDTH * 1.4f * s, Fade(lit, fade)); // the ball
    }

    // Then the rest, in layers from the board up: each note's place, ring and shadow (and the burst of one just
    // played, under the next one coming down on it); the threads; the notes themselves,
    // the soonest first, so one higher up (nearer the eye) is drawn over the ones under it; what's written on them.
    const Color card = themeColor(UiColor::Card);
    const float drop = std::clamp((stringY(lowStringOnTop ? 0 : strings - 1) - radius - area.y) / spacing, MIN_DROP, MAX_DROP) * spacing;
    enum { Board, Bursts, Threads, Notes, Labels, Layers };
    const int shown = (int)(to - from);
    // Where each note still to play is in its fall, and how solid it's drawn: fully, unless it's over one that's due
    // before it (the one under it must show through), and then the less the more it covers it
    struct Falling { bool waiting; float fall, size, depth; Vector2 face; float solid; };
    static std::vector<Falling> falling; // kept between frames: no allocation while playing
    falling.assign(shown, {});
    for (int i = 0; i < shown; i++){
        const PlayNote& note = *(from + i);
        Falling& f = falling[i];
        f.waiting = !note.judged && !note.hit;
        if (!f.waiting) continue;
        float until = note.time - now;
        f.fall = std::clamp(until / approach, 0.0f, 1.0f); // 1 as it appears, 0 once it's down
        float landed = std::clamp(-until / LAND_S, 0.0f, 1.0f); // 0 at the moment it's due, 1 a moment after
        float squash = until <= 0.0f ? 0.08f * std::sin(PI * landed) : 0.0f; // it squashes as it lands
        f.size = radius * (1.0f + DROP_GROW * f.fall) * (1.0f + squash);
        f.depth = NOTE_DEPTH * s * (0.4f + 0.6f * f.fall);
        Vector2 at = centerOf(note);
        f.face = { at.x, at.y - drop * f.fall - f.depth };
        float covering = 0.0f;
        for (int under = 0; under < i; under++){
            if (!falling[under].waiting) continue;
            float dx = f.face.x - falling[under].face.x, dy = f.face.y - falling[under].face.y;
            covering = std::max(covering, std::clamp((2.0f * radius - std::sqrt(dx * dx + dy * dy)) / (COVERS_FULLY * radius), 0.0f, 1.0f));
        }
        f.solid = 1.0f - (1.0f - COVERING_ALPHA) * covering;
    }
    for (int layer = Board; layer < Layers; layer++){
        for (int i = 0; i < shown; i++){
            // What's written on the notes goes over all of them, the soonest note's last: it's the one to read
            const PlayNote& note = layer == Labels ? *(to - 1 - i) : *(from + i);
            Vector2 at = centerOf(note);
            Color color = stringColor(note.stringIndex);
            const float share = std::min(1.0f, note.beats / WHOLE_NOTE_BEATS);
            const bool slider = isSlider(note);
            if (note.hit && slider && now < note.time + note.writtenLength) continue; // held: drawn above
            if (note.hit){
                // It bursts where it was played, green for perfect, brass for good, and is gone
                if (layer != Bursts || note.hitFlash <= 0.0f) continue;
                float t = note.hitFlash / HIT_FLASH_DURATION; // 1 at the hit, 0 when it's over
                Color lit = themeColor(note.wasPerfect ? UiColor::Good : UiColor::Accent);
                float burst = radius + (1.0f - t) * 22 * s;
                smoothCircle(at, radius * (0.6f + 0.6f * t), Fade(lit, t));
                smoothRing(at, burst - 2.5f * s, burst, 0.0f, 360.0f, Fade(lit, t));
                continue;
            }
            if (note.judged){
                // Missed: it stays where it should have been played for a moment, faded, then goes
                float fade = 0.45f * (1.0f - (now - note.time) / MISS_FADE_S);
                if (layer == Board && fade > 0.0f) smoothCircle(at, radius * 0.8f, Fade(color, fade));
                continue;
            }
            const Falling& f = falling[layer == Labels ? shown - 1 - i : i];
            float until = note.time - now, alpha = alphaOf(note), fall = f.fall;
            if (layer == Board){
                // Its place, marked; its shadow, small and faint from high up, gathering as it comes down
                smoothCircle(at, radius, Fade(color, 0.1f * alpha));
                smoothRing(at, radius - 1.5f * s, radius, 0.0f, 360.0f, Fade(color, 0.5f * alpha));
                smoothCircle({ at.x + 1.5f * s, at.y + 2.5f * s }, radius * (1.0f - 0.4f * fall), Fade(themeColor(UiColor::Ink), (0.18f - 0.12f * fall) * alpha));
                if (until > 0.0f){
                    // The ring closes onto the note's rim: when it lands, play it (and a long note's slider begins)
                    // It only shows for the second half of the fall: with notes coming down one after the other onto
                    // one place, a ring for each of them would be a target, not a cue
                    float ring = rim + (radius * RING_START - rim) * until / approach;
                    float near = std::clamp((RING_FROM - fall) / 0.15f, 0.0f, 1.0f);
                    if (near > 0.0f) smoothRing(at, ring - 1.25f * s, ring + 1.25f * s, 0.0f, 360.0f, Fade(color, 0.85f * alpha * near));
                }
                if (slider) drawRimArc(at, rim, 0.0f, share, SLIDER_WIDTH * s, Fade(color, TRACK_ALPHA * alpha)); // how long it rings
                continue;
            }
            if (layer == Bursts) continue;
            // The note: a thick disc seen from above, its side showing under its face
            const float size = f.size, depth = f.depth;
            const Vector2 face = f.face, side = { face.x, face.y + depth };
            if (layer == Threads){
                // A thread down to its place: a note crossing another string's line on its way isn't taken for one on it
                if (fall > 0.0f) smoothLine({ at.x, side.y }, at, 2.0f * s, Fade(color, 0.45f * alpha));
                continue;
            }
            if (layer == Labels){
                // Fainter still on a note that's over another, so it doesn't write over the one about to be played
                drawNoteLabel(note.fret, note.pitch, label, face.x, face.y, size * 1.1f, Fade(WHITE, alpha * f.solid * f.solid));
                continue;
            }
            alpha *= f.solid;
            // Its side is the sliver under its face: drawn as that alone, or it would darken a face that's seen through
            smoothRing(side, size - 2.0f * depth, size, 0.0f, 180.0f, Fade(ColorLerp(color, BLACK, 0.32f), alpha));
            smoothRing(side, size, size + 1.5f * s, 0.0f, 180.0f, Fade(card, alpha)); // a rim that keeps notes apart
            smoothRing(face, size, size + 1.5f * s, 0.0f, 360.0f, Fade(card, alpha));
            smoothCircle(face, size, Fade(color, alpha));
            smoothRing(face, size * 0.72f, size * 0.82f, 200.0f, 330.0f, Fade(WHITE, 0.3f * alpha)); // a glint: a thing, not a mark
        }
    }
    EndScissorMode();
}
