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
const float RING_START = 3.6f;          // a ring starts this many times its note's size
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

// osu!'s way, on the neck: each note fades in on its own place, big, and a big ring closes onto it; when the ring
// meets its rim, play it. The hand's path runs from each note to the next: dots that come in toward the next note
// and go as the song reaches them, so the eye is led on; between two notes close in time, a light runs along it from
// the one played to the one to play, landing on it as it's due. Notes repeated on one spot are stacked as osu!
// stacks them: the one to play now on its place and on top, each after it a little lower and to the right, under
// it, its own ring around it, so how many are coming shows.
const float NOTE_SIZE = 0.48f;     // a note's radius, in the strings' spacing: big, as big as fits between two strings...
const float NOTE_WIDTH = 0.66f;    // ...and in a fret's width, past which it can't grow (it may cover the frets beside it)
const float RING_WIDTH = 3.2f;     // at a 720-pixel-tall window
const float STACK_SHIFT = 0.17f;   // in note sizes, for each note a repeated one is under
const int MAX_STACK = 3;
const float STACK_SETTLE_S = 0.12f; // when the top of a stack is played, the rest move up a step over this time
const float BURST_GROW = 0.5f;     // a note played swells by this much of its size as it fades: osu!'s hit
const float MAX_PATH_S = 2.0f;     // two notes further apart in time than this aren't joined
const float DOT_GAP = 15.0f;       // at a 720-pixel-tall window: between the path's dots
const float DOT_IN_S = 0.2f, DOT_OUT_S = 0.12f; // a dot fades in, and out once the song has reached it
const float FAST_S = 0.6f;         // two notes this close in time: a light runs from one to the next
const int TRAIL = 7;               // the light's tail, in dots

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
    const float radius = std::min(spacing * NOTE_SIZE, fretWidth * NOTE_WIDTH);
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

    // Under the notes: a missed note fading where it should have been played, and the burst of one just played
    for (auto it = from; it != to; ++it){
        const PlayNote& note = *it;
        Vector2 at = centerOf(note);
        if (note.hit){
            if (note.hitFlash <= 0.0f || (isSlider(note) && now < note.time + note.writtenLength)) continue; // held: drawn above
            // The note swells and fades where it was played, quickly at first, then easing out (its ring: below)
            float t = note.hitFlash / HIT_FLASH_DURATION; // 1 at the hit, 0 when it's over
            float eased = 1.0f - t * t * t;               // 0 at the hit, rushing toward 1
            float size = radius * (1.0f + BURST_GROW * eased);
            smoothCircle(at, size, Fade(stringColor(note.stringIndex), 0.85f * t * t));
            smoothCircle(at, size, Fade(WHITE, 0.35f * t * t * t)); // a flash on the hit itself
        } else if (note.judged){
            float fade = 0.45f * (1.0f - (now - note.time) / MISS_FADE_S);
            if (fade > 0.0f) smoothCircle(at, radius * 0.8f, Fade(stringColor(note.stringIndex), fade));
        }
    }

    // The hand's path: one point for each moment a note is due (a chord's lowest string), joined to the next one
    struct Waypoint { float time; Vector2 at; };
    static std::vector<Waypoint> path; // kept between frames: no allocation while playing
    path.clear();
    auto pathFrom = std::lower_bound(notes.begin(), notes.end(), now - MAX_PATH_S, [](const PlayNote& note, float time){ return note.time < time; });
    for (auto it = pathFrom; it != to; ++it){
        if (!path.empty() && it->time - path.back().time < 0.01f) continue; // the same moment: one of a chord's notes
        path.push_back({ it->time, centerOf(*it) });
    }
    const float dotGap = DOT_GAP * s;
    for (size_t i = 0; i + 1 < path.size(); i++){
        const Waypoint a = path[i], b = path[i + 1];
        const float gap = b.time - a.time;
        float dx = b.at.x - a.at.x, dy = b.at.y - a.at.y, length = std::sqrt(dx * dx + dy * dy);
        if (gap > MAX_PATH_S || length < 2.6f * rim) continue; // far apart in time, or on (nearly) the same spot
        const float ux = dx / length, uy = dy / length;
        const float start = rim + 4 * s, end = length - rim - 4 * s;
        // The dots: each has its own moment, along the way from one note's time to the next's. It comes in a note's
        // approach before that, sliding in from behind, and goes as the song reaches it: the path empties toward
        // the next note as the hand should be moving there.
        for (float d = start; d <= end; d += dotGap){
            float own = a.time + gap * (d / length);
            float in = std::clamp((now - (own - approach)) / DOT_IN_S, 0.0f, 1.0f), out = std::clamp(1.0f - (now - own) / DOT_OUT_S, 0.0f, 1.0f);
            float alpha = std::min(in, out);
            if (alpha <= 0.0f) continue;
            float slide = (1.0f - in) * dotGap * 1.5f;
            smoothCircle({ a.at.x + ux * (d - slide), a.at.y + uy * (d - slide) }, 3.2f * s, Fade(themeColor(UiColor::Ink), 0.5f * alpha));
        }
        // Two notes close in time: a light leaves the one played when it's due, and lands on the next as it is
        if (gap <= FAST_S && now >= a.time && now <= b.time){
            float u = (now - a.time) / gap;
            Color light = themeColor(UiColor::Accent);
            for (int k = TRAIL; k >= 0; k--){
                float along = start + (end - start) * std::max(0.0f, u - 0.035f * k);
                float fade = 1.0f - (float)k / (TRAIL + 1);
                smoothCircle({ a.at.x + ux * along, a.at.y + uy * along }, (2.5f + 2.5f * fade) * s, Fade(light, 0.9f * fade));
            }
        }
    }

    // The notes still to play, the last due first, so each is drawn over the ones after it: the one to play now is
    // on top. A note repeated on one spot sits a little lower and to the right of the one before it there.
    const Color card = themeColor(UiColor::Card);
    for (auto it = to; it != from; ){
        --it;
        const PlayNote& note = *it;
        if (note.judged || note.hit) continue;
        // How many are over it on its spot: a note just played there still counts while the stack moves up past it
        float stacked = 0.0f;
        for (auto before = from; before != it; ++before){
            if (before->stringIndex != note.stringIndex || before->fret != note.fret) continue;
            if (!before->judged && !before->hit){ stacked += 1.0f; continue; }
            float left = 1.0f - (now - before->time) / STACK_SETTLE_S;
            if (left > 0.0f){
                left = std::min(1.0f, left);
                stacked += left * left * (3.0f - 2.0f * left); // eased: it starts and ends its move gently
            }
        }
        const float shift = std::min(stacked, (float)MAX_STACK) * STACK_SHIFT * radius;
        const Vector2 at = { centerOf(note).x + shift, centerOf(note).y + shift };
        const Color color = stringColor(note.stringIndex);
        const float alpha = alphaOf(note), until = note.time - now;
        if (isSlider(note)) drawRimArc(at, rim, 0.0f, std::min(1.0f, note.beats / WHOLE_NOTE_BEATS), SLIDER_WIDTH * s, Fade(color, TRACK_ALPHA * alpha)); // how long it rings
        smoothCircle({ at.x + 1.5f * s, at.y + 2.5f * s }, radius, Fade(themeColor(UiColor::Ink), 0.16f * alpha)); // its shadow on the board
        smoothRing(at, radius, radius + 2.0f * s, 0.0f, 360.0f, Fade(card, alpha)); // a rim that keeps notes apart
        smoothCircle(at, radius, Fade(color, alpha));
        smoothRing(at, radius * 0.72f, radius * 0.82f, 200.0f, 330.0f, Fade(WHITE, 0.3f * alpha)); // a glint: a thing, not a mark
        drawNoteLabel(note.fret, note.pitch, label, at.x, at.y, radius * 1.1f, Fade(WHITE, alpha));
        if (until > 0.0f){
            // Its ring, closing onto its rim: when they meet, play it (and a long note's slider begins)
            float ring = rim + (radius * RING_START - rim) * until / approach;
            smoothRing(at, ring - RING_WIDTH * s / 2, ring + RING_WIDTH * s / 2, 0.0f, 360.0f, Fade(color, 0.9f * alpha));
        }
    }

    // Over everything: the ring going out from each note just played, in the judgement's color (green for perfect,
    // brass for good). Only a ring: a note due next on the same spot shows through it.
    for (auto it = from; it != to; ++it){
        const PlayNote& note = *it;
        if (!note.hit || note.hitFlash <= 0.0f || (isSlider(note) && now < note.time + note.writtenLength)) continue;
        float t = note.hitFlash / HIT_FLASH_DURATION, eased = 1.0f - t * t * t;
        float burst = rim + eased * 30 * s;
        smoothRing(centerOf(note), burst - 3.5f * s * t - 0.5f * s, burst, 0.0f, 360.0f, Fade(themeColor(note.wasPerfect ? UiColor::Good : UiColor::Accent), t));
    }
    EndScissorMode();
}
