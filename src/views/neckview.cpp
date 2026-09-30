#include "views/neckview.h"

#include "core/music.h"
#include "core/positions.h"
#include "views/smooth.h"
#include "views/viewfont.h"

#include <algorithm>
#include <cmath>

const int MIN_FRETS = 7;                // the neck shown is never narrower: fewer frets would look like a zoom
const int LAST_FRET = 24;
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
        data = notes.data();
        size = notes.size();
        first = notes.empty() ? 0.0f : notes.front().time;
        last = notes.empty() ? 0.0f : notes.back().time;
    }
    return shape;
}

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
                  const TimeAxis& axis){
    const int strings = (int)tuning.size();
    if (strings == 0) return;
    const float s = GetScreenHeight() / 720.0f;
    const SongShape& shape = shapeOf(notes);
    const FretSpan span = shape.span;
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

    // The latest first, so the next note to play is drawn over any that come after it
    const Color card = themeColor(UiColor::Card);
    for (auto it = to; it != from;){
        const PlayNote& note = *--it;
        Vector2 at = centerOf(note);
        Color color = stringColor(note.stringIndex);
        const float rim = radius + 4.5f * s, share = std::min(1.0f, note.beats / WHOLE_NOTE_BEATS);
        const bool slider = note.beats >= MIN_SLIDER_BEATS && note.writtenLength > 0.0f;
        if (note.hit && slider && now < note.time + note.writtenLength){
            // Held: not a note to play any more, so it can't look like one. After the hit's flash it's hollow, and it
            // greys, shrinks and fades as it rings, while the slider's ball eats its track until the note is over.
            Color lit = themeColor(note.wasPerfect ? UiColor::Good : UiColor::Accent);
            float held = std::max(0.0f, now - note.time);
            float progress = std::clamp(held / note.writtenLength, 0.0f, 1.0f);
            float flash = std::max(0.0f, 1.0f - held / HELD_FLASH_S);
            float fade = 1.0f - 0.75f * progress;
            float size = radius * (0.85f - 0.3f * progress);
            Color grey = ColorLerp(lit, themeColor(UiColor::Dim), 0.4f + 0.6f * progress);
            if (flash > 0.0f) smoothCircle(at, size, Fade(lit, flash));
            smoothRing(at, size - 2.0f * s, size, 0.0f, 360.0f, Fade(grey, fade));
            drawViewText(TextFormat("%d", note.fret), at.x, at.y, size * 1.1f, Fade(grey, fade));
            float played = share * progress;
            drawRimArc(at, rim, played, share, SLIDER_WIDTH * s, Fade(lit, 0.85f * fade));
            float angle = (-90.0f + 360.0f * played) * DEG2RAD;
            smoothCircle({ at.x + rim * std::cos(angle), at.y + rim * std::sin(angle) }, SLIDER_WIDTH * 1.4f * s, Fade(lit, fade)); // the ball
            continue;
        }
        if (note.hit){
            // It bursts where it was played, green for perfect, brass for good, and is gone
            if (note.hitFlash <= 0.0f) continue;
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
            if (fade <= 0.0f) continue;
            smoothCircle(at, radius * 0.8f, Fade(color, fade));
            continue;
        }
        float until = note.time - now, alpha = alphaOf(note);
        if (until > 0.0f){
            // The ring closes onto the note's rim: when it lands, play it (and a long note's slider begins)
            float ring = rim + (radius * RING_START - rim) * until / approach;
            smoothRing(at, ring - 1.25f * s, ring + 1.25f * s, 0.0f, 360.0f, Fade(color, 0.85f * alpha));
        }
        smoothCircle(at, radius + 1.5f * s, Fade(card, alpha)); // a rim that keeps notes apart
        smoothCircle(at, radius, Fade(color, alpha));
        if (slider) drawRimArc(at, rim, 0.0f, share, SLIDER_WIDTH * s, Fade(color, TRACK_ALPHA * alpha)); // how long it rings
        drawViewText(TextFormat("%d", note.fret), at.x, at.y, radius * 1.1f, Fade(WHITE, alpha));
    }
    EndScissorMode();
}
