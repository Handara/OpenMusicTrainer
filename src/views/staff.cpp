#include "views/staff.h"

#include "core/notation.h"
#include "views/smooth.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <string>

// SMuFL code points: the music font standard puts every symbol at the same code in every font
const int GLYPH_TREBLE_CLEF_8VB = 0xE052; // treble clef with a small 8 below: "sounds an octave lower", i.e. guitar
const int GLYPH_TREBLE_CLEF = 0xE050;     // plain: piano, written where it sounds
const int GLYPH_BASS_CLEF = 0xE062;       // bass guitar is written in a plain bass clef, though it too sounds lower
const int GLYPH_NOTEHEAD_WHOLE = 0xE0A2;
const int GLYPH_NOTEHEAD_HALF = 0xE0A3;
const int GLYPH_NOTEHEAD_BLACK = 0xE0A4;
const int GLYPH_FLAT = 0xE260;
const int GLYPH_NATURAL = 0xE261;
const int GLYPH_SHARP = 0xE262;
const int GLYPH_REST_WHOLE = 0xE4E3;     // then the half, quarter, eighth, 16th and 32nd rests: NoteValue order
const int GLYPH_FLAG_8TH_UP = 0xE240;    // then 8th down, 16th up, 16th down, 32nd up, 32nd down
const int GLYPH_AUGMENTATION_DOT = 0xE1E7;
const int GLYPH_TIME_SIGNATURE_0 = 0xE080; // the digits 0 to 9 in a row
const int GLYPH_TUPLET_3 = 0xE883;
// Glyphs are rendered once into a texture at this size, then scaled down when drawn (sharper than scaling up).
// Music fonts have a very tall line height, and raylib sizes fonts by it, so the glyphs themselves come out
// small for a given size: loading big keeps them detailed.
const int FONT_LOAD_SIZE = 768;

// Sizes in staff spaces (the distance between two lines), from the engraving standards SMuFL follows
const float STAFF_SPACES_TALL = 16.0f; // the area's height: the staff, plus room for ledger lines
const float STEM_LENGTH = 3.5f;
const float LINE_THICKNESS = 0.11f;
const float STEM_THICKNESS = 0.12f;
const float BEAM_THICKNESS = 0.5f;
const float BEAM_SPACING = 0.75f;      // from one beam to the next: a beam and a quarter-space gap
const float TIE_THICKNESS = 0.16f;
const float CLEF_MARGIN = 0.5f;        // before the clef, and again after it
const float KEY_ACCIDENTAL_WIDTH = 1.0f;
const float TIME_SIGNATURE_WIDTH = 2.2f;
const float LEAD_MARGIN = 1.0f;        // between the time signature and the hit line

static struct {
    Font font;
    bool loaded = false;
    // Measured once from the loaded font (in pixels at FONT_LOAD_SIZE): where the baseline sits inside a glyph's
    // cell, and the notehead's height. SMuFL makes a black notehead exactly one staff space tall, so its height tells
    // us how big to draw everything, whatever the font's own metrics are.
    float baselineFromTop = 0.0f;
    float noteheadHeight = 1.0f;
} music;

bool loadStaffFont(const std::string& path){
    std::vector<int> codepoints = { GLYPH_TREBLE_CLEF_8VB, GLYPH_TREBLE_CLEF, GLYPH_BASS_CLEF, GLYPH_NOTEHEAD_WHOLE, GLYPH_NOTEHEAD_HALF, GLYPH_NOTEHEAD_BLACK,
                                    GLYPH_FLAT, GLYPH_NATURAL, GLYPH_SHARP, GLYPH_AUGMENTATION_DOT, GLYPH_TUPLET_3 };
    for (int i = 0; i < 6; i++) codepoints.push_back(GLYPH_REST_WHOLE + i);
    for (int i = 0; i < 6; i++) codepoints.push_back(GLYPH_FLAG_8TH_UP + i);
    for (int i = 0; i < 10; i++) codepoints.push_back(GLYPH_TIME_SIGNATURE_0 + i);
    // Loaded by hand rather than with LoadFontEx, for one reason: LoadFontEx packs glyphs into its texture in rows as
    // tall as the font size, which is a music font's huge line height, so the texture overflows and glyphs go
    // missing. Packing method 1 (skyline) places each glyph by its real size.
    int dataSize = 0;
    unsigned char* data = LoadFileData(path.c_str(), &dataSize);
    if (!data) return false;
    Font font{};
    font.baseSize = FONT_LOAD_SIZE;
    font.glyphCount = (int)codepoints.size();
    font.glyphPadding = 4;
    font.glyphs = LoadFontData(data, dataSize, FONT_LOAD_SIZE, codepoints.data(), font.glyphCount, FONT_DEFAULT);
    UnloadFileData(data);
    if (!font.glyphs) return false;
    Image atlas = GenImageFontAtlas(font.glyphs, &font.recs, font.glyphCount, FONT_LOAD_SIZE, font.glyphPadding, 1);
    font.texture = LoadTextureFromImage(atlas);
    UnloadImage(atlas);
    if (font.texture.id == 0){
        UnloadFont(font);
        return false;
    }
    GenTextureMipmaps(&font.texture);                         // smaller copies of the symbols, for drawing small
    SetTextureFilter(font.texture, TEXTURE_FILTER_TRILINEAR);  // smooth at any size, blending between them

    // A SMuFL notehead is centered on its baseline, so the middle of its image is where the baseline is
    int notehead = GetGlyphIndex(font, GLYPH_NOTEHEAD_BLACK);
    music.baselineFromTop = font.glyphs[notehead].offsetY + font.recs[notehead].height / 2.0f;
    music.noteheadHeight = font.recs[notehead].height;
    music.font = font;
    music.loaded = true;
    return true;
}

void unloadStaffFont(){
    if (music.loaded) UnloadFont(music.font);
    music.loaded = false;
}

// --- Glyphs ----------------------------------------------------------------------------------------------------
// Without the font, each glyph gets a plain stand-in, so the staff still reads (roughly)

static bool isNotehead(int glyph){ return glyph >= GLYPH_NOTEHEAD_WHOLE && glyph <= GLYPH_NOTEHEAD_BLACK; }

static float glyphWidth(int glyph, float space){
    if (!music.loaded) return isNotehead(glyph) ? 1.18f * space : space;
    return music.font.recs[GetGlyphIndex(music.font, glyph)].width * space / music.noteheadHeight;
}

// Draws a glyph with the left edge of its ink at x and its baseline at y
static void drawGlyph(int glyph, float x, float y, float space, Color color){
    if (!music.loaded){
        float w = glyphWidth(glyph, space);
        if (glyph == GLYPH_NOTEHEAD_BLACK) DrawEllipse((int)(x + w / 2), (int)y, w / 2, space * 0.45f, color);
        else if (isNotehead(glyph)) DrawEllipseLines((int)(x + w / 2), (int)y, w / 2, space * 0.45f, color);
        else if (glyph == GLYPH_AUGMENTATION_DOT) smoothCircle({x + 0.2f * space, y}, 0.2f * space, color);
        else if (glyph >= GLYPH_REST_WHOLE && glyph <= GLYPH_REST_WHOLE + 5) DrawRectangleRec({x, y - 0.5f * space, w * 0.6f, space}, color);
        else if (glyph >= GLYPH_TIME_SIGNATURE_0 && glyph <= GLYPH_TIME_SIGNATURE_0 + 9) DrawText(TextFormat("%d", glyph - GLYPH_TIME_SIGNATURE_0), (int)x, (int)(y - space), (int)(2 * space), color);
        else if (glyph == GLYPH_SHARP) DrawText("#", (int)x, (int)(y - space), (int)(2 * space), color);
        else if (glyph == GLYPH_FLAT) DrawText("b", (int)x, (int)(y - space), (int)(2 * space), color);
        else if (glyph == GLYPH_NATURAL) DrawText("n", (int)x, (int)(y - space), (int)(2 * space), color);
        else if (glyph == GLYPH_TREBLE_CLEF_8VB || glyph == GLYPH_TREBLE_CLEF) DrawText("G", (int)x, (int)(y - 2 * space), (int)(3 * space), color);
        else if (glyph == GLYPH_BASS_CLEF) DrawText("F", (int)x, (int)(y - space), (int)(3 * space), color);
        else if (glyph == GLYPH_TUPLET_3) DrawText("3", (int)x, (int)(y - space), (int)(1.5f * space), color);
        return; // flags: the stem alone
    }
    float scale = space / music.noteheadHeight;
    int index = GetGlyphIndex(music.font, glyph);
    Vector2 position = { x - music.font.glyphs[index].offsetX * scale, y - music.baselineFromTop * scale };
    DrawTextCodepoint(music.font, glyph, position, FONT_LOAD_SIZE * scale, color);
}

static void drawGlyphCentered(int glyph, float centerX, float y, float space, Color color){
    drawGlyph(glyph, centerX - glyphWidth(glyph, space) / 2, y, space, color);
}

// A time signature number (3, or 12), centered on x
static void drawTimeNumber(int number, float centerX, float y, float space){
    std::string digits = std::to_string(number);
    float width = 0.0f;
    for (char digit : digits) width += glyphWidth(GLYPH_TIME_SIGNATURE_0 + digit - '0', space);
    float x = centerX - width / 2;
    for (char digit : digits){
        int glyph = GLYPH_TIME_SIGNATURE_0 + digit - '0';
        drawGlyph(glyph, x, y, space, themeColor(UiColor::Ink));
        x += glyphWidth(glyph, space);
    }
}

// --- Layout ----------------------------------------------------------------------------------------------------

namespace {

struct Staff {
    Rectangle area;
    float space;       // one staff space, in pixels
    float bottomLineY;
    float thickness;   // of staff and ledger lines
    float yAt(int position) const { return bottomLineY - position * space * 0.5f; }
};

// One notehead of a chord
struct Head {
    int position;
    float x;           // its left edge
    Accidental accidental;
    Color color;
};

// One chord (or single note) as drawn: its heads bottom to top, and its stem
struct Column {
    size_t index;      // of its event in the score
    const ScoreEvent* event;
    int glyph;         // whole, half or black notehead
    float width;       // of one notehead
    std::vector<Head> heads;
    float stemX;
    float stemTipY;
};

} // namespace

static int clefGlyph(const Score& score){
    if (score.clef == Clef::Bass) return GLYPH_BASS_CLEF;
    return score.writtenShift != 0 ? GLYPH_TREBLE_CLEF_8VB : GLYPH_TREBLE_CLEF;
}

// The clef and the room around it, in spaces: the clefs differ in width (the bass clef's dots stick out)
static float clefWidth(const Score& score, float space){
    return CLEF_MARGIN + glyphWidth(clefGlyph(score), space) / space + CLEF_MARGIN;
}

float staffLeadWidth(float areaHeight, const Score& score){
    float space = areaHeight / STAFF_SPACES_TALL;
    int widestKey = 0;
    for (const ScoreBar& bar : score.bars) widestKey = std::max(widestKey, std::abs(bar.key.fifths));
    return (clefWidth(score, space) + widestKey * KEY_ACCIDENTAL_WIDTH + 0.3f + TIME_SIGNATURE_WIDTH + LEAD_MARGIN) * space;
}

float staffBarLineGap(float areaHeight){
    return 2.2f * areaHeight / STAFF_SPACES_TALL; // half a notehead, an accidental, and a little air
}

static void drawKeySignature(const Staff& staff, const KeySignature& key, Clef clef, float x){
    for (int i = 0; i < std::abs(key.fifths); i++){
        int glyph = key.fifths > 0 ? GLYPH_SHARP : GLYPH_FLAT;
        drawGlyph(glyph, x + i * KEY_ACCIDENTAL_WIDTH * staff.space, staff.yAt(keySignaturePosition(key, i, clef)), staff.space, themeColor(UiColor::Ink));
    }
}

static void drawTimeSignature(const Staff& staff, const TimeSignatureChange& time, float centerX){
    drawTimeNumber(time.beats, centerX, staff.yAt(6), staff.space);    // each number fills two spaces:
    drawTimeNumber(time.beatUnit, centerX, staff.yAt(2), staff.space); // the upper and lower halves of the staff
}

// Where a rest is drawn: a whole bar's rest in the middle of its bar, the others at their time
static float restX(const ScoreEvent& rest, float x, float barEndX){
    return rest.wholeBarRest ? (x + barEndX) / 2 : x;
}

static void drawRest(const Staff& staff, const ScoreEvent& rest, float x, float barEndX, Color color){
    int glyph = GLYPH_REST_WHOLE + (int)rest.value;
    x = restX(rest, x, barEndX);
    // A whole rest hangs from the second line down; the others sit on (or are centered on) the middle line
    int position = rest.value == NoteValue::Whole ? 6 : 4;
    drawGlyphCentered(glyph, x, staff.yAt(position), staff.space, color);
    if (rest.dots > 0) drawGlyph(GLYPH_AUGMENTATION_DOT, x + glyphWidth(glyph, staff.space) / 2 + 0.3f * staff.space, staff.yAt(5), staff.space, color);
}

// Draws the chords of one beam group (or one lone chord): noteheads, accidentals, dots, stems, flags or beams, ties.
// `bar` carries the accidentals already written in this bar. Returns the highest point drawn (for tuplet numbers).
// Where each note was last drawn (the middle of its column, over its highest head), for judgements shown over it
static std::vector<Vector2> drawnNotes;
static double drawnAt = -100.0;

// A note's color: lit while it's the one being played, then green if it was hit, red if it was missed
static Color headColor(const PlayNote& note, bool current){
    if (note.judged) return themeColor(note.hit ? UiColor::Good : UiColor::Bad);
    return themeColor(current ? UiColor::Accent : UiColor::Ink);
}

static float drawGroup(const Staff& staff, const Score& score, size_t first, size_t last, const std::vector<PlayNote>& notes,
                       BarAccidentals& bar, const TimeAxis& axis, size_t current){
    const float space = staff.space;
    const KeySignature& key = score.bars[score.events[first].bar].key;
    std::vector<Column> columns;
    int lowest = 1000, highest = -1000;
    for (size_t e = first; e < last; e++){
        const ScoreEvent& event = score.events[e];
        Column column{};
        column.index = e;
        column.event = &event;
        column.glyph = event.value == NoteValue::Whole ? GLYPH_NOTEHEAD_WHOLE : event.value == NoteValue::Half ? GLYPH_NOTEHEAD_HALF : GLYPH_NOTEHEAD_BLACK;
        column.width = glyphWidth(column.glyph, space);
        // A note tied over from the event before shows no accidental: it's the same note, still sounding
        bool tiedOver = e > 0 && score.events[e - 1].tiedToNext && score.events[e - 1].firstNote == event.firstNote;
        for (int n = event.firstNote; n < event.firstNote + event.noteCount; n++){
            const PlayNote& note = notes[n];
            StaffNote written = staffNote(note.pitch + score.writtenShift, key, score.clef);
            Head head{};
            head.position = written.position;
            head.accidental = tiedOver ? Accidental::None : accidentalFor(written, key, bar);
            head.color = headColor(note, e == current);
            column.heads.push_back(head);
        }
        std::sort(column.heads.begin(), column.heads.end(), [](const Head& a, const Head& b){ return a.position < b.position; });
        lowest = std::min(lowest, column.heads.front().position);
        highest = std::max(highest, column.heads.back().position);
        columns.push_back(column);
    }

    // One stem direction for the whole group, set by the note farthest from the middle line
    bool up = STAFF_MIDDLE_LINE - lowest > highest - STAFF_MIDDLE_LINE;
    int mostBeams = 0;
    for (Column& column : columns){
        float x = axis.xAt(column.event->time);
        float left = x - column.width / 2;
        // Notes a step apart (a second) can't share a place: one goes to the other side of the stem
        std::vector<int> side(column.heads.size(), 0);
        if (up){
            for (size_t k = 1; k < column.heads.size(); k++) if (column.heads[k].position - column.heads[k - 1].position == 1 && side[k - 1] == 0) side[k] = 1;
        } else {
            for (size_t k = column.heads.size() - 1; k-- > 0;) if (column.heads[k + 1].position - column.heads[k].position == 1 && side[k + 1] == 0) side[k] = -1;
        }
        for (size_t k = 0; k < column.heads.size(); k++) column.heads[k].x = left + side[k] * column.width;
        column.stemX = up ? left + column.width - STEM_THICKNESS * space / 2 : left + STEM_THICKNESS * space / 2;
        // A stem is 3.5 spaces from the note farthest out, and always reaches the middle line
        column.stemTipY = up ? std::min(staff.yAt(column.heads.back().position) - STEM_LENGTH * space, staff.yAt(STAFF_MIDDLE_LINE))
                             : std::max(staff.yAt(column.heads.front().position) + STEM_LENGTH * space, staff.yAt(STAFF_MIDDLE_LINE));
        mostBeams = std::max(mostBeams, beamCount(column.event->value));
    }
    // A beamed group's stems all end at the beam: level with the stem that reaches farthest, plus room for extra beams
    bool beamed = columns.size() > 1;
    if (beamed){
        float extra = (mostBeams - 1) * BEAM_SPACING * space * 0.5f;
        float beamY = columns[0].stemTipY;
        for (const Column& column : columns) beamY = up ? std::min(beamY, column.stemTipY) : std::max(beamY, column.stemTipY);
        beamY += up ? -extra : extra;
        for (Column& column : columns) column.stemTipY = beamY;
    }

    float top = staff.yAt(STAFF_TOP_LINE);
    for (size_t c = 0; c < columns.size(); c++){
        const Column& column = columns[c];
        const ScoreEvent& event = *column.event;
        float headsLeft = column.heads.front().x, headsRight = column.heads.front().x + column.width;
        for (const Head& head : column.heads){
            headsLeft = std::min(headsLeft, head.x);
            headsRight = std::max(headsRight, head.x + column.width);
        }
        float columnX = axis.xAt(event.time), columnTop = staff.yAt(column.heads.back().position) - 1.2f * space;
        for (int n = event.firstNote; n < event.firstNote + event.noteCount; n++){
            if (n >= 0 && n < (int)drawnNotes.size()) drawnNotes[n] = { columnX, std::min(columnTop, staff.yAt(STAFF_TOP_LINE) - space) };
        }
        for (const Head& head : column.heads){
            // Ledger lines, a little wider than the notehead, on every line between the staff and the note
            float ledgerLeft = head.x - 0.3f * space, ledgerRight = head.x + column.width + 0.3f * space;
            for (int p = -2; p >= head.position; p -= 2) DrawLineEx({ledgerLeft, staff.yAt(p)}, {ledgerRight, staff.yAt(p)}, staff.thickness, themeColor(UiColor::Ink));
            for (int p = STAFF_TOP_LINE + 2; p <= head.position; p += 2) DrawLineEx({ledgerLeft, staff.yAt(p)}, {ledgerRight, staff.yAt(p)}, staff.thickness, themeColor(UiColor::Ink));
            drawGlyph(column.glyph, head.x, staff.yAt(head.position), space, head.color);
            top = std::min(top, staff.yAt(head.position) - space);
        }

        // Accidentals, left of the chord, top to bottom; ones too close to the one above step further left
        std::vector<std::pair<int, int>> placed; // position, column
        for (size_t k = column.heads.size(); k-- > 0;){
            const Head& head = column.heads[k];
            if (head.accidental == Accidental::None) continue;
            int glyph = head.accidental == Accidental::Sharp ? GLYPH_SHARP : head.accidental == Accidental::Flat ? GLYPH_FLAT : GLYPH_NATURAL;
            int accidentalColumn = 0;
            for (auto [position, used] : placed) if (position - head.position < 6) accidentalColumn = std::max(accidentalColumn, used + 1);
            placed.push_back({head.position, accidentalColumn});
            float x = headsLeft - 0.25f * space - glyphWidth(glyph, space) - accidentalColumn * 1.1f * space;
            drawGlyph(glyph, x, staff.yAt(head.position), space, head.color);
        }

        // Dots after the chord, in a space: a note on a line gets its dot in the space above
        if (event.dots > 0){
            for (const Head& head : column.heads){
                int position = head.position % 2 == 0 ? head.position + 1 : head.position;
                drawGlyph(GLYPH_AUGMENTATION_DOT, headsRight + 0.35f * space, staff.yAt(position), space, head.color);
            }
        }

        // The stem, from the note on the far side to the tip, and a flag if it isn't beamed
        if (event.value != NoteValue::Whole){
            float from = up ? staff.yAt(column.heads.front().position) : staff.yAt(column.heads.back().position);
            DrawLineEx({column.stemX, from}, {column.stemX, column.stemTipY}, STEM_THICKNESS * space, column.heads.front().color);
            int beams = beamCount(event.value);
            if (!beamed && beams > 0) drawGlyph(GLYPH_FLAG_8TH_UP + 2 * (beams - 1) + (up ? 0 : 1), column.stemX - STEM_THICKNESS * space / 2, column.stemTipY, space, column.heads.front().color);
            top = std::min(top, column.stemTipY - space);
        }

        // Ties to the next event: curves on the side away from the stem, notehead to notehead
        if (event.tiedToNext && column.index + 1 < score.events.size()){
            const ScoreEvent& next = score.events[column.index + 1]; // a tie always goes to the very next event
            float endX = axis.xAt(next.time) - column.width * 0.6f;
            for (const Head& head : column.heads){
                float y = staff.yAt(head.position) + (up ? 0.6f : -0.6f) * space;
                Vector2 start = { head.x + column.width + 0.1f * space, y };
                Vector2 end = { endX, y };
                Vector2 bend = { (start.x + end.x) / 2, y + (up ? 0.9f : -0.9f) * space };
                DrawSplineSegmentBezierQuadratic(start, bend, end, TIE_THICKNESS * space, themeColor(UiColor::Ink));
            }
        }
    }

    // Beams: the first joins the whole group; sixteenths add a second, 32nds a third, joining neighbours that have
    // one too, or as a short stub where a note has more beams than both its neighbours
    if (beamed){
        for (int level = 1; level <= mostBeams; level++){
            float y = columns[0].stemTipY + (up ? 1.0f : -1.0f) * (level - 1) * BEAM_SPACING * space;
            float thickness = BEAM_THICKNESS * space;
            auto drawBeam = [&](float fromX, float toX){
                float left = std::min(fromX, toX), right = std::max(fromX, toX);
                DrawRectangleRec({left - STEM_THICKNESS * space / 2, up ? y : y - thickness, right - left + STEM_THICKNESS * space, thickness}, themeColor(UiColor::Ink));
            };
            for (size_t c = 0; c < columns.size(); c++){
                bool has = beamCount(columns[c].event->value) >= level;
                bool nextHas = c + 1 < columns.size() && beamCount(columns[c + 1].event->value) >= level;
                bool previousHas = c > 0 && beamCount(columns[c - 1].event->value) >= level;
                if (has && nextHas) drawBeam(columns[c].stemX, columns[c + 1].stemX);
                else if (has && !previousHas){
                    float stub = (c + 1 < columns.size() ? 1.0f : -1.0f) * space; // toward the neighbour inside the group
                    drawBeam(columns[c].stemX, columns[c].stemX + stub);
                }
            }
        }
    }
    return top;
}

// --- The staff ---------------------------------------------------------------------------------------------------
// One bar at a time: the bar being played fills most of the page, the next one waits beside it, small and faded, to
// read ahead. At each bar line the page turns: both slide a place left. In the bar being played the note (or chord,
// or rest) of the moment is lit, with a line under it that fills as it lasts; played notes stay green, or red.

const float MAIN_SHARE = 0.7f;      // of the page, for the bar being played; the next one gets the rest
const float PREVIEW_FADE = 0.55f;   // how faded the next bar is
const float TURN_S = 0.22f;         // the page turn, ending on the new bar's downbeat
const float BAR_PAD = 1.6f;         // spaces inside each end of a bar: room for a downbeat's accidental, and air
const float LIGHT_AHEAD_S = 0.03f;  // a note lights a moment early, so it's lit when it's played

// The events of one bar, placed on `axis`: rests, beam groups, triplet numbers. `current` is the event being
// played (lit), or events.size() for none.
static void drawBarEvents(const Staff& staff, const Score& score, const std::vector<PlayNote>& notes, int barIndex,
                          const TimeAxis& axis, size_t current){
    const std::vector<ScoreEvent>& events = score.events;
    const float space = staff.space;
    size_t start = std::lower_bound(events.begin(), events.end(), barIndex, [](const ScoreEvent& event, int bar){ return event.bar < bar; }) - events.begin();
    BarAccidentals barAccidentals;
    std::vector<std::pair<size_t, float>> tupletTops; // triplet events drawn, with the top of what was drawn for them
    const float barEndX = axis.xAt(score.bars[barIndex + 1].time);
    for (size_t i = start; i < events.size() && events[i].bar == barIndex;){
        const ScoreEvent& event = events[i];
        if (event.rest){
            bool played = event.time + 0.001f < axis.songTime && i != current;
            Color color = i == current ? themeColor(UiColor::Accent) : played ? themeColor(UiColor::Dim) : themeColor(UiColor::Ink);
            drawRest(staff, event, axis.xAt(event.time), barEndX, color);
            if (event.tuplet) tupletTops.push_back({i, staff.yAt(STAFF_TOP_LINE) - space});
            i++;
            continue;
        }
        size_t last = i + 1;
        if (event.beamGroup >= 0) while (last < events.size() && events[last].beamGroup == event.beamGroup) last++;
        float top = drawGroup(staff, score, i, last, notes, barAccidentals, axis, current);
        for (size_t e = i; e < last; e++) if (events[e].tuplet) tupletTops.push_back({e, top});
        i = last;
    }

    // Triplet numbers: a 3 over each triplet beat, above the highest thing drawn in it
    for (size_t k = 0; k < tupletTops.size();){
        const ScoreEvent& first = events[tupletTops[k].first];
        int beat = (first.tick - score.bars[first.bar].tick) / score.resolution;
        float fromX = axis.xAt(first.time), toX = fromX, top = tupletTops[k].second;
        size_t j = k;
        while (j < tupletTops.size()){
            const ScoreEvent& event = events[tupletTops[j].first];
            if (event.bar != first.bar || (event.tick - score.bars[event.bar].tick) / score.resolution != beat) break;
            toX = axis.xAt(event.time);
            top = std::min(top, tupletTops[j].second);
            j++;
        }
        drawGlyphCentered(GLYPH_TUPLET_3, (fromX + toX) / 2, std::min(top, staff.yAt(STAFF_TOP_LINE + 2)) - 0.3f * space, space, themeColor(UiColor::Ink));
        k = j;
    }
}

// The glow behind the event being played, and under the staff the line that fills as it lasts, to the next event
static void drawLit(const Staff& staff, const Score& score, size_t current, const TimeAxis& axis){
    const ScoreEvent& event = score.events[current];
    const float space = staff.space;
    float endTime = current + 1 < score.events.size() ? score.events[current + 1].time : score.bars[event.bar + 1].time;
    float x = axis.xAt(event.time);
    if (event.rest) x = restX(event, x, axis.xAt(score.bars[event.bar + 1].time));
    float progress = std::clamp((axis.songTime - event.time) / std::max(0.01f, endTime - event.time), 0.0f, 1.0f);
    float pop = std::max(0.0f, 1.0f - (axis.songTime - event.time) / 0.18f); // it swells as it starts
    float half = (1.5f + 0.35f * pop) * space;
    Rectangle glow = { x - half, staff.yAt(STAFF_TOP_LINE + 5) - pop * 0.5f * space, 2 * half, staff.yAt(-5) - staff.yAt(STAFF_TOP_LINE + 5) + pop * space };
    smoothRoundedRect(glow, 0.9f * space, Fade(themeColor(UiColor::Accent), 0.13f + 0.12f * pop));
    // The line: from this event to the next, filling as it lasts (a rest's too: silence is played as well)
    float from = axis.xAt(event.time) - 0.6f * space, to = std::max(from + space, axis.xAt(endTime) - 1.2f * space);
    float y = staff.yAt(-6), thickness = 0.35f * space;
    smoothRoundedRect({ from, y - thickness / 2, to - from, thickness }, thickness / 2, Fade(themeColor(UiColor::Accent), 0.22f));
    smoothRoundedRect({ from, y - thickness / 2, (to - from) * progress, thickness }, thickness / 2, themeColor(UiColor::Accent));
}

void drawStaff(Rectangle area, const std::vector<PlayNote>& notes, const Score& score, const TimeAxis& axis){
    Staff staff;
    staff.area = area;
    staff.space = area.height / STAFF_SPACES_TALL;
    staff.bottomLineY = area.y + area.height - 5.0f * staff.space; // leaves room for the low E's three ledger lines
    staff.thickness = std::max(1.0f, LINE_THICKNESS * staff.space);
    const float space = staff.space;
    const float right = area.x + area.width;
    drawnNotes.assign(notes.size(), Vector2{ -1.0f, -1.0f });
    drawnAt = GetTime();

    DrawRectangleRec(area, themeColor(UiColor::Card));
    auto drawStaffLines = [&](float fromX, float toX){
        for (int line = 0; line <= STAFF_TOP_LINE; line += 2) DrawLineEx({fromX, staff.yAt(line)}, {toX, staff.yAt(line)}, staff.thickness, themeColor(UiColor::Ink));
    };
    const int barCount = (int)score.bars.size() - 1; // the last bar line only closes the music
    if (barCount <= 0){
        drawStaffLines(area.x, right);
        return;
    }

    // The bar being played, and how far the page has turned toward the next (the turn ends on its downbeat)
    const float now = axis.songTime + LIGHT_AHEAD_S;
    int barNow = 0;
    while (barNow + 1 < barCount && score.bars[barNow + 1].time <= now) barNow++;
    float turn = 0.0f;
    if (barNow + 1 < barCount){
        float t = std::clamp((now - (score.bars[barNow + 1].time - TURN_S)) / TURN_S, 0.0f, 1.0f);
        turn = t * t * (3.0f - 2.0f * t);
    }
    const float page = barNow + turn;

    // The clef, key and time signature at the left, for the bar in front
    const ScoreBar& front = score.bars[std::min(barCount - 1, (int)std::lround(page))];
    const float leadRight = area.x + staffLeadWidth(area.height, score) - LEAD_MARGIN * space;
    drawStaffLines(area.x, right - 0.8f * space);
    drawGlyph(clefGlyph(score), area.x + CLEF_MARGIN * space, staff.yAt(score.clef == Clef::Bass ? 6 : 2), space, themeColor(UiColor::Ink));
    drawKeySignature(staff, front.key, score.clef, area.x + clefWidth(score, space) * space);
    drawTimeSignature(staff, front.timeSignature, leadRight - TIME_SIGNATURE_WIDTH * space / 2);

    // Each bar's place on the page by how far it is from the front: 0 in front, 1 waiting beside it, -1 gone left
    const float pageLeft = leadRight + 0.6f * space, pageRight = right - 0.8f * space, pageWidth = pageRight - pageLeft;
    const float mainWidth = pageWidth * MAIN_SHARE, previewWidth = pageWidth - mainWidth;
    auto placeAt = [&](float d, float& x, float& width, float& alpha){
        if (d <= 0.0f){ // front, sliding out to the left
            x = pageLeft + d * mainWidth; width = mainWidth; alpha = 1.0f + d;
        } else if (d <= 1.0f){ // from beside it to the front
            x = pageLeft + d * mainWidth; width = mainWidth + (previewWidth - mainWidth) * d; alpha = 1.0f - (1.0f - PREVIEW_FADE) * d;
        } else { // coming in from the right
            x = pageLeft + mainWidth + (d - 1.0f) * previewWidth; width = previewWidth; alpha = PREVIEW_FADE * (2.0f - d);
        }
    };

    for (int k = std::max(0, barNow - 1); k <= std::min(barCount - 1, barNow + 2); k++){
        float d = k - page, x, width, alpha;
        if (d <= -1.0f || d >= 2.0f) continue;
        placeAt(d, x, width, alpha);
        if (alpha <= 0.01f) continue;
        float barStart = score.bars[k].time, barEnd = score.bars[k + 1].time;
        TimeAxis barAxis;
        barAxis.songTime = axis.songTime;
        barAxis.noteSpeed = (width - 2 * BAR_PAD * space) / std::max(0.01f, barEnd - barStart);
        barAxis.hitLineX = x + BAR_PAD * space + (axis.songTime - barStart) * barAxis.noteSpeed; // xAt(barStart) = x + pad
        float clipLeft = std::max(x, pageLeft - 0.2f * space), clipRight = std::min(x + width, pageRight);
        if (clipRight <= clipLeft) continue;
        BeginScissorMode((int)clipLeft, (int)area.y, (int)(clipRight - clipLeft + 1), (int)area.height);
        // The event being played, in the bar being played
        size_t current = score.events.size();
        if (k == barNow && now >= barStart){
            for (size_t e = 0; e < score.events.size() && score.events[e].time <= now; e++) if (score.events[e].bar == k) current = e;
        }
        if (current < score.events.size()) drawLit(staff, score, current, barAxis);
        drawBarEvents(staff, score, notes, k, barAxis, current);
        // Its closing bar line (the song's last, doubled)
        bool closing = k + 1 == barCount;
        float lineX = x + width;
        DrawLineEx({lineX, staff.yAt(STAFF_TOP_LINE)}, {lineX, staff.yAt(0)}, staff.thickness * (closing ? 4.0f : 1.4f), themeColor(UiColor::Ink));
        if (closing) DrawLineEx({lineX - 0.6f * space, staff.yAt(STAFF_TOP_LINE)}, {lineX - 0.6f * space, staff.yAt(0)}, staff.thickness * 1.4f, themeColor(UiColor::Ink));
        // Faded as it waits (or leaves): the paper laid thinly over it
        if (alpha < 1.0f) DrawRectangleRec({x, area.y, width, area.height}, Fade(themeColor(UiColor::Card), 1.0f - alpha));
        EndScissorMode();
    }
}

bool staffNoteAt(int noteIndex, float& x, float& y){
    if (GetTime() - drawnAt > 0.25 || noteIndex < 0 || noteIndex >= (int)drawnNotes.size() || drawnNotes[noteIndex].x < 0.0f) return false;
    x = drawnNotes[noteIndex].x;
    y = drawnNotes[noteIndex].y;
    return true;
}
