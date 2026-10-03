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

// A note's shape: a rounded square, which fills a fret's cell between two strings better than a circle (it can be
// big and still leave room between notes on neighboring strings), or a circle. Everything drawn around a note (its
// ring, its slider, its burst) follows the same shape.
const bool SQUARE_NOTES = true;
const float CORNER = 0.32f; // a square's corners, rounded by this much of its half-width

static void fillShape(Vector2 at, float half, Color color){
    if (SQUARE_NOTES) smoothRoundedRect({ at.x - half, at.y - half, 2 * half, 2 * half }, CORNER * half, color);
    else smoothCircle(at, half, color);
}

// The way round a shape's outline, `half` from its middle, clockwise from the top: at `along` (0 to 1, one loop)
// where it is; and drawn from `from` to `to`, `width` thick. A square's outline is its straight sides and its rounded
// corners, each drawn on its own, end to end.
struct Outline {
    Vector2 at;
    float half;
    float straight() const { return half * (1.0f - CORNER); } // half a side's straight part
    float corner() const { return half * CORNER; }
    float length() const { return SQUARE_NOTES ? 8 * straight() + 2 * PI * corner() : 2 * PI * half; }
};

static void drawOutline(const Outline& o, float from, float to, float width, Color color){
    if (to <= from) return;
    if (!SQUARE_NOTES){
        smoothRing(o.at, o.half - width / 2, o.half + width / 2, -90.0f + 360.0f * from, -90.0f + 360.0f * to, color);
        return;
    }
    const float i = o.straight(), c = o.corner(), arc = PI / 2 * c, total = o.length();
    const float x = o.at.x, y = o.at.y, h = o.half;
    // The pieces, clockwise from the top's middle: a straight piece from a to b, or a corner's quarter round about
    // its center from an angle (0 right, clockwise)
    struct Piece { bool round; Vector2 a, b; Vector2 center; float angle; float length; };
    const Piece pieces[] = {
        { false, { x, y - h }, { x + i, y - h }, {}, 0, i },
        { true, {}, {}, { x + i, y - i }, 270, arc },
        { false, { x + h, y - i }, { x + h, y + i }, {}, 0, 2 * i },
        { true, {}, {}, { x + i, y + i }, 0, arc },
        { false, { x + i, y + h }, { x - i, y + h }, {}, 0, 2 * i },
        { true, {}, {}, { x - i, y + i }, 90, arc },
        { false, { x - h, y + i }, { x - h, y - i }, {}, 0, 2 * i },
        { true, {}, {}, { x - i, y - i }, 180, arc },
        { false, { x - i, y - h }, { x, y - h }, {}, 0, i },
    };
    float start = 0.0f;
    for (const Piece& piece : pieces){
        float a = std::max(from * total, start), b = std::min(to * total, start + piece.length);
        if (b > a && piece.length > 0.0f){
            float u0 = (a - start) / piece.length, u1 = (b - start) / piece.length;
            if (piece.round){
                smoothRing(piece.center, c - width / 2, c + width / 2, piece.angle + 90 * u0, piece.angle + 90 * u1, color);
            } else {
                Vector2 p0 = { piece.a.x + (piece.b.x - piece.a.x) * u0, piece.a.y + (piece.b.y - piece.a.y) * u0 };
                Vector2 p1 = { piece.a.x + (piece.b.x - piece.a.x) * u1, piece.a.y + (piece.b.y - piece.a.y) * u1 };
                smoothLine(p0, p1, width, color);
            }
        }
        start += piece.length;
    }
}

static Vector2 outlinePoint(const Outline& o, float along){
    if (!SQUARE_NOTES){
        float angle = (-90.0f + 360.0f * along) * DEG2RAD;
        return { o.at.x + o.half * std::cos(angle), o.at.y + o.half * std::sin(angle) };
    }
    const float i = o.straight(), c = o.corner(), arc = PI / 2 * c, x = o.at.x, y = o.at.y, h = o.half;
    float d = std::clamp(along, 0.0f, 1.0f) * o.length();
    const float lengths[] = { i, arc, 2 * i, arc, 2 * i, arc, 2 * i, arc, i };
    int piece = 0;
    while (piece < 8 && d > lengths[piece]){ d -= lengths[piece]; piece++; }
    float u = lengths[piece] > 0 ? d / lengths[piece] : 0.0f;
    auto corner = [&](float cx, float cy, float angle){
        float r = (angle + 90 * u) * DEG2RAD;
        return Vector2{ cx + c * std::cos(r), cy + c * std::sin(r) };
    };
    switch (piece){
        case 0: return { x + i * u, y - h };
        case 1: return corner(x + i, y - i, 270);
        case 2: return { x + h, y - i + 2 * i * u };
        case 3: return corner(x + i, y + i, 0);
        case 4: return { x + i - 2 * i * u, y + h };
        case 5: return corner(x - i, y + i, 90);
        case 6: return { x - h, y + i - 2 * i * u };
        case 7: return corner(x - i, y - i, 180);
        default: return { x - i + i * u, y - h };
    }
}

// How far from a note's middle its outline is, going in a direction (a unit vector)
static float edgeToward(float half, float ux, float uy){
    if (!SQUARE_NOTES) return half;
    return half / std::max(0.7f, std::max(std::fabs(ux), std::fabs(uy))); // the corners, rounded, don't reach the square's
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

// osu!'s way, on the neck: each note fades in on its own place and a big ring closes onto it; when the ring
// meets its rim, play it. The hand's path runs from each note to the next: dots that come in toward the next note
// and go as the song reaches them, so the eye is led on; between two notes close in time, a light runs along it from
// the one played to the one to play, landing on it as it's due. Notes repeated on one spot sit exactly on it, the
// one to play now on top, each with its own ring around it: the rings, one inside the other, say how many are coming.
const float NOTE_SIZE = 0.34f;     // a note's half-width, in the smaller of a fret's width and the strings' spacing: room
                                   // is left between notes on neighboring strings and frets for the path between them
const float RING_WIDTH = 3.2f;     // at a 720-pixel-tall window
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
    const float radius = std::min(spacing, fretWidth) * NOTE_SIZE;
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
        if (flash > 0.0f) fillShape(at, radius * (0.6f + 0.4f * flash), Fade(lit, flash));
        float played = share * progress;
        drawOutline({ at, rim }, played, share, SLIDER_WIDTH * s, Fade(grey, 0.85f * fade));
        smoothCircle(outlinePoint({ at, rim }, played), SLIDER_WIDTH * 1.4f * s, Fade(lit, fade)); // the ball
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
            fillShape(at, size, Fade(stringColor(note.stringIndex), 0.85f * t * t));
            fillShape(at, size, Fade(WHITE, 0.35f * t * t * t)); // a flash on the hit itself
        } else if (note.judged){
            float fade = 0.45f * (1.0f - (now - note.time) / MISS_FADE_S);
            if (fade > 0.0f) fillShape(at, radius * 0.8f, Fade(stringColor(note.stringIndex), fade));
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
        if (gap > MAX_PATH_S || length < 1e-3f) continue; // far apart in time, or on the same spot
        const float ux = dx / length, uy = dy / length;
        const float edge = edgeToward(rim, ux, uy);
        if (length < 2 * edgeToward(radius, ux, uy) + 4 * s) continue; // touching: there's no way between them to draw
        float start = edge + 4 * s, end = length - edge - 4 * s;
        if (end - start < dotGap){ start = end = length / 2; } // close: one dot, midway, still says which way
        // The dots: each has its own moment, along the way from one note's time to the next's. It comes in a note's
        // approach before that, sliding in from behind, and goes as the song reaches it: the path empties toward
        // the next note as the hand should be moving there.
        for (float d = start; d <= end + 0.01f; d += dotGap){
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
    // on top.
    const Color card = themeColor(UiColor::Card);
    for (auto it = to; it != from; ){
        --it;
        const PlayNote& note = *it;
        if (note.judged || note.hit) continue;
        const Vector2 at = centerOf(note);
        const Color color = stringColor(note.stringIndex);
        const float alpha = alphaOf(note), until = note.time - now;
        if (isSlider(note)) drawOutline({ at, rim }, 0.0f, std::min(1.0f, note.beats / WHOLE_NOTE_BEATS), SLIDER_WIDTH * s, Fade(color, TRACK_ALPHA * alpha)); // how long it rings
        fillShape({ at.x + 1.5f * s, at.y + 2.5f * s }, radius, Fade(themeColor(UiColor::Ink), 0.16f * alpha)); // its shadow on the board
        fillShape(at, radius + 2.0f * s, Fade(card, alpha)); // a rim that keeps notes apart
        fillShape(at, radius, Fade(color, alpha));
        if (!SQUARE_NOTES) smoothRing(at, radius * 0.72f, radius * 0.82f, 200.0f, 330.0f, Fade(WHITE, 0.3f * alpha)); // a glint: a thing, not a mark
        drawNoteLabel(note.fret, note.pitch, label, at.x, at.y, radius * 1.3f, Fade(WHITE, alpha));
        if (until > 0.0f){
            // Its ring, closing onto its rim: when they meet, play it (and a long note's slider begins)
            float ring = rim + (radius * RING_START - rim) * until / approach;
            drawOutline({ at, ring }, 0.0f, 1.0f, RING_WIDTH * s, Fade(color, 0.9f * alpha));
        }
    }

    // Over everything: the ring going out from each note just played, in the judgement's color (green for perfect,
    // brass for good). Only a ring: a note due next on the same spot shows through it.
    for (auto it = from; it != to; ++it){
        const PlayNote& note = *it;
        if (!note.hit || note.hitFlash <= 0.0f || (isSlider(note) && now < note.time + note.writtenLength)) continue;
        float t = note.hitFlash / HIT_FLASH_DURATION, eased = 1.0f - t * t * t;
        float burst = rim + eased * 30 * s;
        drawOutline({ centerOf(note), burst - (3.5f * s * t + 0.5f * s) / 2 }, 0.0f, 1.0f, 3.5f * s * t + 0.5f * s, Fade(themeColor(note.wasPerfect ? UiColor::Good : UiColor::Accent), t));
    }
    EndScissorMode();
}
