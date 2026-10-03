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

// A note's shape: a rectangle with rounded corners, wider than tall, like the fret's cell it sits in. A circle is the
// same shape with as wide as tall and corners rounded all the way: NOTE_ASPECT 1 and CORNER 1. Everything drawn
// around a note (its ring, its slider, its burst) is the same shape, grown or shrunk by a margin.
const float NOTE_ASPECT = 1.5f; // width against height
const float CORNER = 0.4f;      // corners rounded by this much of the half-height

// A shape: its middle, half its width and height, and its corners' radius
struct Outline {
    Vector2 at;
    float w, h, c;
};

static void fillShape(const Outline& o, Color color){
    smoothRoundedRect({ o.at.x - o.w, o.at.y - o.h, 2 * o.w, 2 * o.h }, o.c, color);
}

// Its outline, clockwise from the top's middle: straight sides and quarter-rounds at the corners. A straight piece
// runs from a to b; a round one turns about `center` from `angle` (degrees, 0 right, clockwise) through 90.
struct Piece {
    bool round;
    Vector2 a, b, center;
    float angle, length;
};

static int piecesOf(const Outline& o, Piece pieces[9]){
    const float x = o.at.x, y = o.at.y, w = o.w, h = o.h, c = o.c;
    const float iw = std::max(0.0f, w - c), ih = std::max(0.0f, h - c), arc = PI / 2 * c;
    const Piece all[9] = {
        { false, { x, y - h }, { x + iw, y - h }, {}, 0, iw },
        { true, {}, {}, { x + iw, y - ih }, 270, arc },
        { false, { x + w, y - ih }, { x + w, y + ih }, {}, 0, 2 * ih },
        { true, {}, {}, { x + iw, y + ih }, 0, arc },
        { false, { x + iw, y + h }, { x - iw, y + h }, {}, 0, 2 * iw },
        { true, {}, {}, { x - iw, y + ih }, 90, arc },
        { false, { x - w, y + ih }, { x - w, y - ih }, {}, 0, 2 * ih },
        { true, {}, {}, { x - iw, y - ih }, 180, arc },
        { false, { x - iw, y - h }, { x, y - h }, {}, 0, iw },
    };
    std::copy(all, all + 9, pieces);
    return 9;
}

static float outlineLength(const Outline& o){
    Piece pieces[9];
    float total = 0.0f;
    for (int i = 0, n = piecesOf(o, pieces); i < n; i++) total += pieces[i].length;
    return total;
}

static Vector2 pointOn(const Piece& piece, float c, float u){
    if (!piece.round) return { piece.a.x + (piece.b.x - piece.a.x) * u, piece.a.y + (piece.b.y - piece.a.y) * u };
    float r = (piece.angle + 90 * u) * DEG2RAD;
    return { piece.center.x + c * std::cos(r), piece.center.y + c * std::sin(r) };
}

// The outline from `from` to `to` (0 to 1: one loop), `width` thick
static void drawOutline(const Outline& o, float from, float to, float width, Color color){
    if (to <= from) return;
    Piece pieces[9];
    const int n = piecesOf(o, pieces);
    const float total = outlineLength(o);
    float start = 0.0f;
    for (int i = 0; i < n; i++){
        const Piece& piece = pieces[i];
        float a = std::max(from * total, start), b = std::min(to * total, start + piece.length);
        if (b > a && piece.length > 0.0f){
            float u0 = (a - start) / piece.length, u1 = (b - start) / piece.length;
            if (piece.round) smoothRing(piece.center, o.c - width / 2, o.c + width / 2, piece.angle + 90 * u0, piece.angle + 90 * u1, color);
            else smoothLine(pointOn(piece, o.c, u0), pointOn(piece, o.c, u1), width, color);
        }
        start += piece.length;
    }
}

// Where on the outline `along` is (0 to 1: one loop, clockwise from the top's middle)
static Vector2 outlinePoint(const Outline& o, float along){
    Piece pieces[9];
    const int n = piecesOf(o, pieces);
    float d = std::clamp(along, 0.0f, 1.0f) * outlineLength(o);
    for (int i = 0; i < n; i++){
        if (d <= pieces[i].length || i == n - 1) return pointOn(pieces[i], o.c, pieces[i].length > 0 ? std::min(1.0f, d / pieces[i].length) : 0.0f);
        d -= pieces[i].length;
    }
    return o.at;
}

// How far from its middle the shape's edge is, going in a direction (a unit vector); its corners aside
static float edgeToward(const Outline& o, float ux, float uy){
    float across = std::fabs(ux) > 1e-4f ? o.w / std::fabs(ux) : 1e9f, down = std::fabs(uy) > 1e-4f ? o.h / std::fabs(uy) : 1e9f;
    return std::min(across, down);
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

// osu!'s way, on the neck: each note fades in on its own place and a big ring (the note's shape) closes onto it; when the ring
// meets its rim, play it. The hand's path runs from each note to the next: dots that come in toward the next note
// and go as the song reaches them, so the eye is led on; between two notes close in time, a light runs along it from
// the one played to the one to play, landing on it as it's due. Notes repeated on one spot sit exactly on it, the
// one to play now on top, each with its own ring around it: the rings, one inside the other, say how many are coming.
const float NOTE_HEIGHT = 0.27f;   // a note's half-height, in the strings' spacing...
const float NOTE_WIDTH = 0.38f;    // ...and its half-width at most, in a fret's: room is left between notes on
                                   // neighboring strings and frets for the path between them
const float RIM = 4.5f;            // at a 720-pixel-tall window: from a note's edge to its ring's and slider's
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
    const float noteW = std::min(spacing * NOTE_HEIGHT * NOTE_ASPECT, fretWidth * NOTE_WIDTH), noteH = noteW / NOTE_ASPECT;
    const float radius = noteH; // what's sized by the note (a burst, a ring's travel) goes by its height
    // The note's shape at `at`, grown by `grow` pixels all round (shrunk, below 0), its corners growing with it
    auto noteShape = [&](Vector2 at, float grow){
        float w = std::max(0.5f, noteW + grow), h = std::max(0.5f, noteH + grow);
        return Outline{ at, w, h, std::min(std::max(0.0f, CORNER * noteH + grow), std::min(w, h)) };
    };
    lastNeck = { GetTime(), boardLeft, nut, fretWidth, top, spacing, radius, strings, firstFretted, lowStringOnTop };
    auto stringY = [&](int string){ return top + spacing * (0.5f + (lowStringOnTop ? string : strings - 1 - string)); };
    auto fretX = [&](int fret){
        if (fret == 0) return boardLeft + fretWidth * OPEN_COLUMN_SHARE / 2;
        return nut + (fret - firstFretted + 0.5f) * fretWidth;
    };

    BeginScissorMode((int)area.x, (int)area.y, (int)area.width, (int)area.height);
    // The board: a dark pane with a fine edge, its strings lit faintly in their colors
    const bool night = currentTheme() == ThemeMode::Dark;
    const Rectangle pane = { boardLeft - 6 * s, top, boardRight - boardLeft + 12 * s, boardHeight };
    smoothRoundedRect({ pane.x - 1.0f * s, pane.y - 1.0f * s, pane.width + 2.0f * s, pane.height + 2.0f * s }, 6 * s, themeColor(UiColor::StaffLine));
    smoothRoundedRect(pane, 5 * s, themeColor(UiColor::Card));
    const float middle = top + boardHeight / 2;
    const Color line = themeColor(UiColor::StaffLine);
    for (int fret = firstFretted; fret <= span.last; fret++){
        if (std::count(std::begin(SINGLE_DOTS), std::end(SINGLE_DOTS), fret)) smoothCircle({ fretX(fret), middle }, 5 * s, line);
        if (fret == 12 || fret == 24){
            smoothCircle({ fretX(fret), middle - spacing }, 5 * s, line);
            smoothCircle({ fretX(fret), middle + spacing }, 5 * s, line);
        }
        float wire = std::round(nut + (fret - firstFretted + 1) * fretWidth);
        if (fret < span.last) DrawRectangleRec({ wire - 0.75f * s, top + 4 * s, 1.5f * s, boardHeight - 8 * s }, Fade(themeColor(UiColor::Dim), 0.35f));
        drawViewText(TextFormat("%d", fret), fretX(fret), top + boardHeight + numbersHeight / 2 + 2 * s, 13 * s, themeColor(UiColor::Dim));
    }
    // The nut, thick, when the open strings are shown; else the first shown fret's wire
    DrawRectangleRec({ nut - (open ? 2.0f : 0.75f) * s, top + 4 * s, (open ? 4.0f : 1.5f) * s, boardHeight - 8 * s },
                     open ? Fade(themeColor(UiColor::Ink), 0.85f) : Fade(themeColor(UiColor::Dim), 0.35f));
    for (int string = 0; string < strings; string++){
        float y = stringY(string), thickness = (1.0f + 0.3f * (strings - 1 - string)) * s; // lower strings are thicker
        const Color lit = stringColor(string);
        if (night) DrawRectangleRec({ boardLeft, y - thickness * 2.0f, boardRight - boardLeft, thickness * 4.0f }, Fade(lit, 0.06f)); // its glow
        DrawRectangleRec({ boardLeft, y - thickness / 2, boardRight - boardLeft, thickness }, Fade(lit, night ? 0.55f : 0.7f));
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
    const float rim = RIM * s; // the margin, from a note's edge, of its slider and its ring once closed
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
        if (flash > 0.0f) fillShape(noteShape(at, -0.4f * (1.0f - flash) * radius), Fade(lit, flash));
        float played = share * progress;
        drawOutline(noteShape(at, rim), played, share, SLIDER_WIDTH * s, Fade(grey, 0.85f * fade));
        smoothCircle(outlinePoint(noteShape(at, rim), played), SLIDER_WIDTH * 1.4f * s, Fade(lit, fade)); // the ball
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
            const Outline swollen = noteShape(at, radius * BURST_GROW * eased);
            fillShape(swollen, Fade(stringColor(note.stringIndex), 0.85f * t * t));
            fillShape(swollen, Fade(WHITE, 0.35f * t * t * t)); // a flash on the hit itself
        } else if (note.judged){
            float fade = 0.45f * (1.0f - (now - note.time) / MISS_FADE_S);
            if (fade > 0.0f) fillShape(noteShape(at, -0.2f * radius), Fade(stringColor(note.stringIndex), fade));
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
        const float edge = edgeToward(noteShape(a.at, rim), ux, uy);
        if (length < 2 * edgeToward(noteShape(a.at, 0.0f), ux, uy) + 4 * s) continue; // touching: there's no way between them to draw
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
            smoothCircle({ a.at.x + ux * (d - slide), a.at.y + uy * (d - slide) }, 3.0f * s, Fade(themeColor(UiColor::Accent), 0.65f * alpha));
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
    // on top. Each is a pane of dark glass edged in its string's neon, glowing; the next to play (all of a chord's
    // notes) is lit almost white, so it's found at once among the rest. By day: tinted glass, and the next one solid.
    float nextTime = 1e9f;
    for (auto it = from; it != to; ++it) if (!it->judged && !it->hit){ nextTime = it->time; break; }
    const Color card = themeColor(UiColor::Card), background = themeColor(UiColor::Background);
    const Color whiteHot = { 236, 246, 255, 255 };
    for (auto it = to; it != from; ){
        --it;
        const PlayNote& note = *it;
        if (note.judged || note.hit) continue;
        const Vector2 at = centerOf(note);
        const Color color = stringColor(note.stringIndex);
        const float alpha = alphaOf(note), until = note.time - now;
        const bool next = note.time - nextTime < 0.01f;
        if (isSlider(note)) drawOutline(noteShape(at, rim), 0.0f, std::min(1.0f, note.beats / WHOLE_NOTE_BEATS), SLIDER_WIDTH * s, Fade(color, TRACK_ALPHA * alpha)); // how long it rings
        // Its glow, fading out from its edge (stronger on the next one)
        const float glow = next ? 0.4f : 0.22f;
        for (int k = 1; k <= 3; k++) drawOutline(noteShape(at, 1.6f * k * s), 0.0f, 1.0f, 2.0f * s, Fade(next ? whiteHot : color, alpha * glow / k));
        fillShape(noteShape(at, 1.0f * s), Fade(background, alpha)); // a dark rim that keeps notes apart
        Color fill, ink;
        if (next){
            fill = night ? whiteHot : color;
            ink = night ? background : WHITE;
        } else {
            fill = ColorLerp(card, color, night ? 0.16f : 0.14f);
            ink = night ? ColorLerp(color, WHITE, 0.35f) : ColorLerp(color, BLACK, 0.2f);
        }
        fillShape(noteShape(at, 0.0f), Fade(fill, alpha));
        drawOutline(noteShape(at, -1.0f * s), 0.0f, 1.0f, (next ? 2.6f : 2.0f) * s, Fade(color, alpha)); // its neon edge
        drawNoteLabel(note.fret, note.pitch, label, at.x, at.y, radius * 1.45f, Fade(ink, alpha));
        if (until > 0.0f){
            // Its ring, closing onto its rim: when they meet, play it (and a long note's slider begins)
            float ring = rim + ((RING_START - 1.0f) * radius - rim) * until / approach;
            const Color ringColor = next && night ? ColorLerp(color, whiteHot, 0.5f) : color;
            drawOutline(noteShape(at, ring + 2.0f * s), 0.0f, 1.0f, 4.0f * s, Fade(ringColor, 0.12f * alpha)); // its glow
            drawOutline(noteShape(at, ring), 0.0f, 1.0f, 2.2f * s, Fade(ringColor, 0.95f * alpha));
        }
    }

    // Over everything: the ring going out from each note just played, in the judgement's color (green for perfect,
    // brass for good). Only a ring: a note due next on the same spot shows through it.
    for (auto it = from; it != to; ++it){
        const PlayNote& note = *it;
        if (!note.hit || note.hitFlash <= 0.0f || (isSlider(note) && now < note.time + note.writtenLength)) continue;
        float t = note.hitFlash / HIT_FLASH_DURATION, eased = 1.0f - t * t * t;
        float width = 3.5f * s * t + 0.5f * s, burst = rim + eased * 30 * s - width / 2;
        drawOutline(noteShape(centerOf(note), burst), 0.0f, 1.0f, width, Fade(themeColor(note.wasPerfect ? UiColor::Good : UiColor::Accent), t));
    }
    EndScissorMode();
}
