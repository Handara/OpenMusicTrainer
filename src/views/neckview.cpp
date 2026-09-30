#include "views/neckview.h"

#include "core/music.h"
#include "core/positions.h"
#include "rlgl.h"
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

// In 3D, the board is drawn flat into an image first, then that image is laid on a plane tilted away from the
// player, seen in perspective: the far strings smaller, the near ones bigger. This is where a point of the image
// lands on the screen.
const float TILT_DEGREES = 32.0f;   // the board leaning back
const float VIEW_DISTANCE = 1.5f;   // the eye's distance from the board's middle, in board image heights
const int TILT_GRID_X = 32, TILT_GRID_Y = 12; // the image is laid in this many pieces, so its perspective is true
const float TILTED_STRING_SPACING = 120.0f; // the strings spread further before the board leans back and shortens
const float LIFT = 3.2f;            // a note starts this many radii above its place, and drops onto it with its ring

struct Tilt {
    bool on = false;
    float width = 0, height = 0;    // the image
    float cosT = 1, sinT = 0, distance = 1;
    float fit = 1, centerX = 0, centerY = 0;
    // The image point x, y on the screen, and how much bigger things are drawn there than in the image
    Vector2 project(float x, float y, float* scale = nullptr) const {
        if (!on){
            if (scale) *scale = 1.0f;
            return { x, y };
        }
        float across = x - width / 2, along = y - height / 2;
        float depth = -along * sinT; // the top of the board leans away
        float f = distance / (distance + depth) * fit;
        if (scale) *scale = f;
        return { centerX + across * f, centerY + along * cosT * f };
    }
};

static Tilt tiltFor(Rectangle area, float width, float height){
    Tilt tilt;
    tilt.on = true;
    tilt.width = width;
    tilt.height = height;
    tilt.cosT = std::cos(TILT_DEGREES * DEG2RAD);
    tilt.sinT = std::sin(TILT_DEGREES * DEG2RAD);
    tilt.distance = VIEW_DISTANCE * height;
    // Fitted into the area: its near (bottom) edge is the widest
    Vector2 topLeft = tilt.project(0, 0), bottomRight = tilt.project(width, height);
    Vector2 bottomLeft = tilt.project(0, height);
    float spanX = bottomRight.x - bottomLeft.x, spanY = bottomRight.y - topLeft.y;
    tilt.fit = std::min(area.width / spanX, area.height / spanY);
    float middleY = (topLeft.y + bottomRight.y) / 2;
    tilt.centerX = area.x + area.width / 2;
    tilt.centerY = area.y + area.height / 2 - middleY * tilt.fit;
    return tilt;
}

// A note standing up off the board, drawn after the tilted board so it isn't squashed with it
struct Standing {
    Vector2 at;     // its place, in the board image
    float time;
    float radius;
    float lift;     // how high above its place, in radii
    Color color;
    float alpha;
    int fret, pitch;
};

// The layout the neck was last drawn with, so where a note is can be asked afterwards (neckNoteAt)
static struct {
    double drawnAt = -100.0; // GetTime
    float boardLeft = 0, nut = 0, fretWidth = 0, top = 0, spacing = 0, radius = 0;
    int strings = 0, firstFretted = 1;
    bool lowStringOnTop = false;
    Tilt tilt;
} lastNeck;

bool neckNoteAt(const PlayNote& note, float& x, float& y, float& radius){
    if (GetTime() - lastNeck.drawnAt > 0.25) return false; // not on screen now
    float imageX = note.fret == 0 ? lastNeck.boardLeft + lastNeck.fretWidth * OPEN_COLUMN_SHARE / 2
                                  : lastNeck.nut + (note.fret - lastNeck.firstFretted + 0.5f) * lastNeck.fretWidth;
    float imageY = lastNeck.top + lastNeck.spacing * (0.5f + (lastNeck.lowStringOnTop ? note.stringIndex : lastNeck.strings - 1 - note.stringIndex));
    float scale;
    Vector2 at = lastNeck.tilt.project(imageX, imageY, &scale);
    x = at.x;
    y = at.y;
    radius = lastNeck.radius * scale;
    return true;
}

// The neck as seen from straight above, in `area`: on the screen, or into the image the 3D view tilts. With
// `standing`, the notes still to play aren't drawn but listed there (their shadows are), to stand up off the tilted
// board afterwards.
static void drawNeck(Rectangle area, const std::vector<PlayNote>& notes, const std::vector<int>& tuning, bool lowStringOnTop,
                     bool wholeNeck, NoteLabel label, const TimeAxis& axis, std::vector<Standing>* standing){
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
    const float spacing = std::min((standing ? TILTED_STRING_SPACING : MAX_STRING_SPACING) * s, (area.height - 2 * pad - numbersHeight) / strings);
    const float boardHeight = spacing * strings;
    const float top = area.y + (area.height - boardHeight - numbersHeight) / 2;
    const float radius = std::min(fretWidth, spacing) * 0.36f;
    lastNeck = { GetTime(), boardLeft, nut, fretWidth, top, spacing, radius, strings, firstFretted, lowStringOnTop, Tilt{} };
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

    // Then the rest, the latest first, so the next note to play is drawn over any that come after it
    const Color card = themeColor(UiColor::Card);
    for (auto it = to; it != from;){
        const PlayNote& note = *--it;
        Vector2 at = centerOf(note);
        Color color = stringColor(note.stringIndex);
        const float share = std::min(1.0f, note.beats / WHOLE_NOTE_BEATS);
        const bool slider = isSlider(note);
        if (note.hit && slider && now < note.time + note.writtenLength) continue; // held: drawn above
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
        if (slider) drawRimArc(at, rim, 0.0f, share, SLIDER_WIDTH * s, Fade(color, TRACK_ALPHA * alpha)); // how long it rings
        if (standing){
            // Up off the board, dropping onto its place as its ring closes; its shadow grows and darkens under it
            float lift = LIFT * std::pow(std::clamp(until / approach, 0.0f, 1.0f), 1.6f);
            smoothCircle(at, radius * (1.0f - 0.25f * lift / LIFT), Fade(themeColor(UiColor::Ink), 0.18f * alpha * (1.0f - 0.6f * lift / LIFT)));
            standing->push_back({ at, note.time, radius, lift, color, alpha, note.fret, note.pitch });
            continue;
        }
        smoothCircle(at, radius + 1.5f * s, Fade(card, alpha)); // a rim that keeps notes apart
        smoothCircle(at, radius, Fade(color, alpha));
        drawNoteLabel(note.fret, note.pitch, label, at.x, at.y, radius * 1.1f, Fade(WHITE, alpha));
    }
    EndScissorMode();
}

void drawNeckView(Rectangle area, const std::vector<PlayNote>& notes, const std::vector<int>& tuning, bool lowStringOnTop,
                  bool wholeNeck, bool threeD, NoteLabel label, const TimeAxis& axis){
    if (!threeD){
        drawNeck(area, notes, tuning, lowStringOnTop, wholeNeck, label, axis, nullptr);
        return;
    }
    // The board drawn flat into an image the size of the area, on the screen's own background, so its edges don't
    // show where the tilted image ends
    static RenderTexture2D image{};
    const int width = (int)area.width, height = (int)area.height;
    if (width <= 0 || height <= 0) return;
    if (image.texture.width != width || image.texture.height != height){
        if (image.id != 0) UnloadRenderTexture(image);
        image = LoadRenderTexture(width, height);
        SetTextureFilter(image.texture, TEXTURE_FILTER_BILINEAR);
    }
    std::vector<Standing> standing;
    BeginTextureMode(image);
    ClearBackground(themeColor(UiColor::Background));
    drawNeck({ 0, 0, (float)width, (float)height }, notes, tuning, lowStringOnTop, wholeNeck, label, axis, &standing);
    EndTextureMode();
    const Tilt tilt = tiltFor(area, (float)width, (float)height);
    lastNeck.tilt = tilt;

    // Laid on the tilted plane in small pieces, each flat on the screen: together, the plane in true perspective.
    // (The image is upside down, as render textures are.)
    rlSetTexture(image.texture.id);
    rlBegin(RL_QUADS);
    rlColor4ub(255, 255, 255, 255);
    for (int gy = 0; gy < TILT_GRID_Y; gy++){
        for (int gx = 0; gx < TILT_GRID_X; gx++){
            float u0 = (float)gx / TILT_GRID_X, u1 = (float)(gx + 1) / TILT_GRID_X;
            float v0 = (float)gy / TILT_GRID_Y, v1 = (float)(gy + 1) / TILT_GRID_Y;
            Vector2 a = tilt.project(u0 * width, v0 * height), b = tilt.project(u0 * width, v1 * height);
            Vector2 c = tilt.project(u1 * width, v1 * height), d = tilt.project(u1 * width, v0 * height);
            rlTexCoord2f(u0, 1.0f - v0); rlVertex2f(a.x, a.y);
            rlTexCoord2f(u0, 1.0f - v1); rlVertex2f(b.x, b.y);
            rlTexCoord2f(u1, 1.0f - v1); rlVertex2f(c.x, c.y);
            rlTexCoord2f(u1, 1.0f - v0); rlVertex2f(d.x, d.y);
        }
    }
    rlEnd();
    rlSetTexture(0);

    // The notes to play, standing up: the far ones first, so the near ones stand in front of them; at one place, the
    // later first, so the next to play is in front
    std::sort(standing.begin(), standing.end(), [](const Standing& a, const Standing& b){
        return a.at.y != b.at.y ? a.at.y < b.at.y : a.time > b.time;
    });
    const Color card = themeColor(UiColor::Card);
    const float s = GetScreenHeight() / 720.0f;
    for (const Standing& note : standing){
        float scale;
        Vector2 at = tilt.project(note.at.x, note.at.y, &scale);
        float radius = note.radius * scale;
        at.y -= note.lift * note.radius * scale;
        smoothCircle(at, radius + 1.5f * s * scale, Fade(card, note.alpha));
        smoothCircle(at, radius, Fade(note.color, note.alpha));
        smoothRing(at, radius * 0.78f, radius * 0.86f, 200.0f, 340.0f, Fade(WHITE, 0.35f * note.alpha)); // a glint: it's a thing, not a mark
        drawNoteLabel(note.fret, note.pitch, label, at.x, at.y, radius * 1.1f, Fade(WHITE, note.alpha));
    }
}
