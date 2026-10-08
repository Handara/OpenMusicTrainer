#include "learn/lessonpage.h"

#include "audio/audio.h"
#include "core/drill.h"
#include "core/music.h"
#include "core/notation.h"
#include "core/score.h"
#include "ui/fretboardview.h"
#include "ui/pianoboard.h"
#include "ui/theme.h"
#include "video/video.h"
#include "views/staff.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <sstream>

namespace fs = std::filesystem;

// At a 720-pixel-tall window; everything scales with it
const float PAGE_TITLE_SIZE = 30.0f;
const float HEADING_SIZE = 24.0f;
const float TEXT_SIZE = 18.0f;
const float CAPTION_SIZE = 15.0f;
const float SECTION_GAP = 26.0f;
const float COLUMN_GAP = 30.0f;
const float BLOCK_GAP = 16.0f;
const float PARAGRAPH_GAP = 9.0f;
const float MAX_PICTURE_HEIGHT = 0.45f; // of the window's height: a picture never pushes everything else off the page

void releasePageMedia(PageMedia& media){
    for (auto& [path, texture] : media.textures) UnloadTexture(texture);
    if (!media.audioPath.empty()) unloadSong();
    if (!media.videoPath.empty()) closeVideo();
    media = PageMedia{};
}

namespace {

// What every block's drawing needs
struct BlockContext {
    ImDrawList* draw;
    const std::string& folder;
    PageMedia& media;
    ExerciseInstrument instrument; // the lesson's: whose neck, whose sound
    bool editing;
    float s;
};

// In the lesson maker: a dashed box where something will go, saying what
float drawPlaceholder(ImDrawList* draw, ImVec2 at, float width, float height, const char* text, float s){
    const ImVec2 b(at.x + width, at.y + height);
    const ImU32 color = uiColor(UiColor::Dim, 0.7f);
    const float dash = 8 * s, gap = 6 * s;
    for (float x = at.x; x < b.x; x += dash + gap){
        horizontalLine(draw, x, std::min(x + dash, b.x), at.y, 1.5f * s, color);
        horizontalLine(draw, x, std::min(x + dash, b.x), b.y, 1.5f * s, color);
    }
    for (float y = at.y; y < b.y; y += dash + gap){
        verticalLine(draw, at.x, y, std::min(y + dash, b.y), 1.5f * s, color);
        verticalLine(draw, b.x, y, std::min(y + dash, b.y), 1.5f * s, color);
    }
    const ImVec2 extent = uiFonts().text->CalcTextSizeA(15 * s, FLT_MAX, width - 20 * s, text);
    draw->AddText(uiFonts().text, 15 * s, ImVec2(at.x + (width - extent.x) / 2, at.y + (height - extent.y) / 2), uiColor(UiColor::Dim), text, nullptr,
                  width - 20 * s);
    return height;
}

const std::vector<int> GUITAR_TUNING = { 40, 45, 50, 55, 59, 64 };
const std::vector<int> BASS_TUNING = { 28, 33, 38, 43 };

// A note heard on the lesson's instrument: now, or at a time on the engine's clock
void soundNote(ExerciseInstrument instrument, int pitch, double at = -1.0){
    const float frequency = midiToFrequency((float)pitch);
    const bool bass = instrument == ExerciseInstrument::Bass;
    if (instrument == ExerciseInstrument::Piano){
        if (at < 0.0) playKeysNote(frequency);
        else playKeysNoteAt(frequency, at);
    } else if (at < 0.0) playStringNote(frequency, bass, 1.2f, 0.8f);
    else playStringNoteAt(frequency, bass, 1.0f, at, 0.8f);
}

// Text with a little markup: **bold**, and [E4] a note as a chip, heard when it's clicked
struct TextRun {
    std::string text;
    bool bold = false;
    int pitch = -1;          // a note's chip; -1 for words
    bool spaceBefore = false;
};

std::vector<TextRun> textRuns(const std::string& paragraph){
    std::vector<TextRun> runs;
    std::string word;
    bool bold = false, space = false;
    auto flush = [&](){
        if (word.empty()) return;
        runs.push_back({ word, bold, -1, space });
        space = false;
        word.clear();
    };
    for (size_t i = 0; i < paragraph.size(); i++){
        const char c = paragraph[i];
        if (c == ' '){
            flush();
            space = true;
        } else if (paragraph.compare(i, 2, "**") == 0){
            flush();
            bold = !bold;
            i++;
        } else if (c == '['){
            const size_t close = paragraph.find(']', i);
            int pitch;
            if (close != std::string::npos && parseNoteName(paragraph.substr(i + 1, close - i - 1), pitch)){
                flush();
                runs.push_back({ paragraph.substr(i + 1, close - i - 1), bold, pitch, space });
                space = false;
                i = close;
            } else word += c;
        } else word += c;
    }
    flush();
    return runs;
}

// A paragraph laid out word by word, wrapped to the width; drawn when `draw` is given (else only measured). Returns
// its height.
float richParagraph(ImDrawList* draw, ImVec2 at, float width, float size, ImU32 color, const std::string& paragraph,
                    ExerciseInstrument instrument, float s){
    const UiFonts& fonts = uiFonts();
    const float lineHeight = size * 1.38f, space = fonts.text->CalcTextSizeA(size, FLT_MAX, 0.0f, " ").x, chipPad = 7 * s;
    float x = 0.0f, y = 0.0f;
    const std::vector<TextRun> runs = textRuns(paragraph);
    for (const TextRun& run : runs){
        ImFont* font = run.pitch >= 0 || run.bold ? fonts.bold : fonts.text;
        const float w = font->CalcTextSizeA(size, FLT_MAX, 0.0f, run.text.c_str()).x + (run.pitch >= 0 ? 2 * chipPad : 0.0f);
        float gap = run.spaceBefore ? space : 0.0f;
        if (x > 0.0f && x + gap + w > width){
            x = 0.0f;
            y += lineHeight;
            gap = 0.0f;
        }
        x += gap;
        if (draw){
            const ImVec2 a(at.x + x, at.y + y);
            if (run.pitch >= 0){
                const ImVec2 b(a.x + w, a.y + lineHeight - 3 * s);
                const bool hovered = ImGui::IsMouseHoveringRect(a, b);
                draw->AddRectFilled(ImVec2(a.x, a.y + 1 * s), b, uiColor(UiColor::Accent, hovered ? 0.32f : 0.16f), 6 * s);
                draw->AddText(font, size, ImVec2(a.x + chipPad, a.y + (lineHeight - size) / 2 - 1 * s), uiColor(UiColor::Accent), run.text.c_str());
                if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) soundNote(instrument, run.pitch);
            } else draw->AddText(font, size, ImVec2(a.x, a.y + (lineHeight - size) / 2), color, run.text.c_str());
        }
        x += w;
    }
    return runs.empty() ? 0.0f : y + lineHeight;
}

// Paragraphs of it, a little apart
float richParagraphs(ImDrawList* draw, ImVec2 at, float width, float size, ImU32 color, const std::vector<std::string>& paragraphs,
                     ExerciseInstrument instrument, float s){
    float y = 0.0f;
    for (size_t i = 0; i < paragraphs.size(); i++){
        if (i > 0) y += PARAGRAPH_GAP * s;
        y += richParagraph(draw, ImVec2(at.x, at.y + y), width, size, color, paragraphs[i], instrument, s);
    }
    return y;
}

float textHeight(ImFont* font, float size, float width, const std::string& text){
    return font->CalcTextSizeA(size, FLT_MAX, width, text.c_str()).y;
}

// A pill-shaped button drawn on the page; true when clicked
bool pageButton(ImDrawList* draw, ImVec2 at, const char* label, bool lit, float s){
    const UiFonts& fonts = uiFonts();
    const ImVec2 extent = fonts.bold->CalcTextSizeA(15 * s, FLT_MAX, 0.0f, label);
    const ImVec2 b(at.x + extent.x + 28 * s, at.y + 32 * s);
    const bool hovered = ImGui::IsMouseHoveringRect(at, b);
    draw->AddRectFilled(at, b, lit ? uiColor(UiColor::Accent) : hovered ? uiColor(UiColor::Accent, 0.15f) : uiColor(UiColor::Card), 16 * s);
    draw->AddRect(at, b, uiColor(lit || hovered ? UiColor::Accent : UiColor::StaffLine), 16 * s, 0, 1.0f);
    draw->AddText(fonts.bold, 15 * s, ImVec2(at.x + 14 * s, at.y + (32 * s - extent.y) / 2), uiColor(lit ? UiColor::Background : UiColor::Ink), label);
    return hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left);
}

// "1:05 / 3:20"
std::string timeText(double position, double length){
    return TextFormat("%d:%02d / %d:%02d", (int)position / 60, (int)position % 60, (int)length / 60, (int)length % 60);
}

// A picture as big as the column (never taller than MAX_PICTURE_HEIGHT of the window), keeping its shape
float drawPicture(const BlockContext& c, const Texture2D& texture, ImVec2 at, float width){
    const float maxHeight = ImGui::GetIO().DisplaySize.y * MAX_PICTURE_HEIGHT;
    const float scale = std::min(width / std::max(1, texture.width), maxHeight / std::max(1, texture.height));
    const float w = texture.width * scale, h = texture.height * scale, x = at.x + (width - w) / 2;
    c.draw->AddImageRounded(ImTextureID(texture.id), ImVec2(x, at.y), ImVec2(x + w, at.y + h), ImVec2(0, 0), ImVec2(1, 1), IM_COL32_WHITE, 8 * c.s);
    return h;
}

const Texture2D* pictureFor(PageMedia& media, const std::string& path){
    auto found = media.textures.find(path);
    if (found != media.textures.end()) return &found->second;
    Texture2D texture = LoadTexture(path.c_str());
    if (texture.id == 0){
        media.error = "Couldn't load " + fs::path(path).filename().string();
        return nullptr;
    }
    SetTextureFilter(texture, TEXTURE_FILTER_BILINEAR); // smooth at any size
    return &(media.textures[path] = texture);
}

float drawCaption(const BlockContext& c, const LessonBlock& block, ImVec2 at, float width){
    const std::string caption = blockValue(block, "caption");
    if (caption.empty()) return 0.0f;
    c.draw->AddText(uiFonts().text, CAPTION_SIZE * c.s, ImVec2(at.x, at.y + 6 * c.s), uiColor(UiColor::Dim), caption.c_str(), nullptr, width);
    return 6 * c.s + textHeight(uiFonts().text, CAPTION_SIZE * c.s, width, caption);
}

float drawText(const BlockContext& c, const LessonBlock& block, ImVec2 at, float width){
    return richParagraphs(c.draw, at, width, TEXT_SIZE * c.s, uiColor(UiColor::Ink), blockValues(block, "text"), c.instrument, c.s);
}

// Its button, until pressed; then what it hid
float drawReveal(const BlockContext& c, const LessonBlock& block, ImVec2 at, float width, const std::string& key){
    if (c.editing){ // its button's words, then what it hides
        const std::string label = "HIDDEN BEHIND  \"" + blockValue(block, "label") + "\"";
        c.draw->AddText(uiFonts().mono, 12 * c.s, at, uiColor(UiColor::Dim), label.c_str());
        return 20 * c.s + richParagraphs(c.draw, ImVec2(at.x, at.y + 20 * c.s), width, TEXT_SIZE * c.s, uiColor(UiColor::Ink, 0.75f),
                                         blockValues(block, "text"), c.instrument, c.s);
    }
    if (!c.media.revealed.count(key)){
        if (pageButton(c.draw, at, blockValue(block, "label").c_str(), false, c.s)) c.media.revealed.insert(key);
        return 32 * c.s;
    }
    return richParagraphs(c.draw, at, width, TEXT_SIZE * c.s, uiColor(UiColor::Ink), blockValues(block, "text"), c.instrument, c.s);
}

// Part of the neck, its notes marked (their names on them, or their own labels; the lit ones in the accent colour).
// A note clicked is heard.
float drawNeckBlock(const BlockContext& c, const LessonBlock& block, ImVec2 at, float width){
    const float s = c.s;
    const std::string whose = blockValue(block, "instrument");
    const bool bass = whose == "bass" || (whose == "lesson" && c.instrument == ExerciseInstrument::Bass);
    const std::vector<int>& tuning = bass ? BASS_TUNING : GUITAR_TUNING;
    int first = 0, last = 5;
    std::istringstream(blockValue(block, "frets")) >> first >> last;
    const float namesRoom = 26 * s; // the strings' names, left of the board
    const FretboardLayout board = fretboardLayout(at.x + namesRoom, at.y, width - namesRoom, s, (int)tuning.size(), first, std::max(first + 1, last), 26.0f);
    drawFretboard(board, tuning);
    const bool names = blockValue(block, "labels") == "names";
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    auto mark = [&](const std::vector<NeckPlace>& places, bool lit){
        for (const NeckPlace& place : places){
            if (place.string >= (int)tuning.size() || place.fret < first || place.fret > last) continue;
            const int pitch = tuning[(size_t)place.string] + place.fret;
            const std::string name = !place.label.empty() ? place.label : names ? pitchClassName(pitch) : "";
            drawFretDot(board, place.string, place.fret, 11 * s, uiColor(lit ? UiColor::Accent : UiColor::Ink), uiColor(UiColor::Background), name.c_str());
            const float dx = mouse.x - board.fretX(place.fret), dy = mouse.y - board.stringY(place.string);
            if (dx * dx + dy * dy < 13 * s * 13 * s && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) soundNote(bass ? ExerciseInstrument::Bass : ExerciseInstrument::Guitar, pitch);
        }
    };
    mark(readNeckPlaces(blockValue(block, "dots")), false);
    mark(readNeckPlaces(blockValue(block, "lit")), true);
    const float height = board.height + 24 * s; // the fret numbers under it
    return height + drawCaption(c, block, ImVec2(at.x, at.y + height), width);
}

// Piano keys from one note to another (whole octaves from a C), some lit with their names; a key clicked is heard
float drawKeyboardBlock(const BlockContext& c, const LessonBlock& block, ImVec2 at, float width){
    int low = 60, high = 72;
    parseNoteName(blockValue(block, "from"), low);
    parseNoteName(blockValue(block, "to"), high);
    const std::vector<int> lit = readNotes(blockValue(block, "lit"));
    const bool names = blockValue(block, "labels") == "names";
    const PianoBoard keys = pianoBoard(at.x, at.y, width, std::min(width * 0.3f, 150 * c.s), std::min(low, high), std::max(low, high), true);
    const int clicked = drawPianoBoard(keys, c.s, [&](int pitch){
        PianoKeyStyle look;
        if (std::find(lit.begin(), lit.end(), pitch) != lit.end()){
            look.fill = mixColor(pianoKeyColor(pitch - keys.firstPitch), uiColor(UiColor::Accent), 0.85f);
            if (names) look.label = pitchClassName(pitch);
        }
        return look;
    });
    if (clicked >= 0) soundNote(ExerciseInstrument::Piano, clicked);
    return keys.height + drawCaption(c, block, ImVec2(at.x, at.y + keys.height), width);
}

// Notes on a staff, a beat each (two bars of them), in the lesson's instrument's clef; and a button to hear them
float drawStaffBlock(const BlockContext& c, const LessonBlock& block, ImVec2 at, float width){
    const float s = c.s, height = 120 * s;
    const bool piano = c.instrument == ExerciseInstrument::Piano, bass = c.instrument == ExerciseInstrument::Bass;
    const std::vector<int> tuning = piano ? std::vector<int>{ 0 } : bass ? BASS_TUNING : GUITAR_TUNING;
    std::vector<int> pitches = readNotes(blockValue(block, "notes"));
    if (pitches.size() > 8) pitches.resize(8);
    std::istringstream keyWords(blockValue(block, "key"));
    std::string tonic, mode;
    KeySignature key;
    keyWords >> tonic >> mode;
    parseKeySignature(tonic, mode, key);
    std::vector<DrillNote> notes;
    for (size_t i = 0; i < pitches.size(); i++) notes.push_back({ (double)i, 0, pitches[i] - tuning[0], pitches[i] });
    Chart chart = drillChart(notes, tuning, key, 4);
    const Score score = buildScore(chart, chart.frettedTracks[0]);
    std::vector<PlayNote> shown;
    for (const DrillNote& note : notes){
        PlayNote play{ (float)note.beat, note.stringIndex, note.fret, note.pitch };
        play.beats = 1.0f;
        shown.push_back(play);
    }
    TimeAxis axis;
    axis.songTime = -0.2f; // before its first note (lit a moment ahead of it): nothing lit
    axis.hitLineX = at.x;
    axis.noteSpeed = 100.0f;
    // Drawn with raylib, under the page's clipping: kept inside it the same way
    const ImVec2 clipMin = c.draw->GetClipRectMin(), clipMax = c.draw->GetClipRectMax();
    const Rectangle clip{ clipMin.x, clipMin.y, clipMax.x - clipMin.x, clipMax.y - clipMin.y };
    BeginScissorMode((int)clip.x, (int)clip.y, (int)clip.width, (int)clip.height);
    DrawRectangleRounded({ at.x, at.y, width, height }, 0.08f, 8, themeColor(UiColor::Card));
    EndScissorMode();
    setStaffClip(&clip);
    drawStaff({ at.x, at.y, width, height }, shown, score, axis);
    setStaffClip(nullptr);
    float y = height;
    if (blockValue(block, "listen") == "yes"){
        y += 8 * s;
        if (pageButton(c.draw, ImVec2(at.x, at.y + y), "Listen", false, s)){
            const double start = audioTime() + 0.1;
            for (size_t i = 0; i < pitches.size(); i++) soundNote(c.instrument, pitches[i], start + i * 0.5);
        }
        y += 32 * s;
    }
    return y + drawCaption(c, block, ImVec2(at.x, at.y + y), width);
}

float drawHeading(const BlockContext& c, const LessonBlock& block, ImVec2 at, float width){
    const std::string text = blockValue(block, "text");
    c.draw->AddText(uiFonts().heavy, HEADING_SIZE * c.s, at, uiColor(UiColor::Ink), text.c_str(), nullptr, width);
    return textHeight(uiFonts().heavy, HEADING_SIZE * c.s, width, text);
}

// A box of its own: a bar down its left in its kind's colour, its kind in small capitals, then its text
float drawCallout(const BlockContext& c, const LessonBlock& block, ImVec2 at, float width){
    const float s = c.s, pad = 16 * s, bar = 4 * s;
    const std::string style = blockValue(block, "style");
    const UiColor color = style == "careful" ? UiColor::Bad : style == "remember" ? UiColor::Good : UiColor::Accent;
    const char* label = style == "careful" ? "CAREFUL" : style == "remember" ? "REMEMBER" : "TIP";
    const float inner = width - 2 * pad - bar;
    // Measured first: the box goes under the text
    const std::vector<std::string> paragraphs = blockValues(block, "text");
    const float textTall = richParagraphs(nullptr, ImVec2(0, 0), inner, 17 * s, 0, paragraphs, c.instrument, s);
    const float height = pad + 20 * s + textTall + pad;
    c.draw->AddRectFilled(at, ImVec2(at.x + width, at.y + height), uiColor(color, 0.08f), 8 * s);
    c.draw->AddRectFilled(at, ImVec2(at.x + bar, at.y + height), uiColor(color), 8 * s, ImDrawFlags_RoundCornersLeft);
    c.draw->AddText(uiFonts().mono, 13 * s, ImVec2(at.x + bar + pad, at.y + pad), uiColor(color), label);
    richParagraphs(c.draw, ImVec2(at.x + bar + pad, at.y + pad + 20 * s), inner, 17 * s, uiColor(UiColor::Ink), paragraphs, c.instrument, s);
    return height;
}

float drawImageBlock(const BlockContext& c, const LessonBlock& block, ImVec2 at, float width){
    if (blockValue(block, "file").empty()) return c.editing ? drawPlaceholder(c.draw, at, width, 120 * c.s, "A picture: choose its file", c.s) : 0.0f;
    const Texture2D* texture = pictureFor(c.media, (fs::path(c.folder) / blockValue(block, "file")).string());
    float height = texture ? drawPicture(c, *texture, at, width) : 0.0f;
    return height + drawCaption(c, block, ImVec2(at.x, at.y + height), width);
}

// A sound: Listen (or Stop) and where it's got to; through the song stream, so starting it stops any other
float drawAudioBlock(const BlockContext& c, const LessonBlock& block, ImVec2 at, float width){
    if (blockValue(block, "file").empty()) return c.editing ? drawPlaceholder(c.draw, at, width, 60 * c.s, "A sound: choose its file", c.s) : 0.0f;
    const std::string path = (fs::path(c.folder) / blockValue(block, "file")).string();
    const bool playing = c.media.audioPath == path && !songEnded();
    if (pageButton(c.draw, at, playing ? "Stop" : "Listen", playing, c.s)){
        if (!c.media.audioPath.empty()) unloadSong();
        if (!c.media.videoPath.empty()){ closeVideo(); c.media.videoPath.clear(); }
        c.media.audioPath.clear();
        std::string error;
        if (!playing){
            if (loadSong(path, error)){
                playSong(false);
                c.media.audioPath = path;
            } else c.media.error = error;
        }
    }
    if (c.media.audioPath == path)
        c.draw->AddText(uiFonts().mono, 14 * c.s, ImVec2(at.x + 110 * c.s, at.y + 8 * c.s), uiColor(UiColor::Dim), timeText(songPosition(), songLength()).c_str());
    return 32 * c.s + drawCaption(c, block, ImVec2(at.x, at.y + 32 * c.s), width);
}

// A video: its first picture until Play; Stop closes it (it opens again on its first picture)
float drawVideoBlock(const BlockContext& c, const LessonBlock& block, ImVec2 at, float width){
    if (blockValue(block, "file").empty()) return c.editing ? drawPlaceholder(c.draw, at, width, 120 * c.s, "A video: choose its file", c.s) : 0.0f;
    const std::string path = (fs::path(c.folder) / blockValue(block, "file")).string();
    if (c.media.videoPath != path){
        if (!c.media.videoPath.empty()) closeVideo();
        if (!c.media.audioPath.empty()){ unloadSong(); c.media.audioPath.clear(); } // its sound needs the song stream
        c.media.videoPath.clear();
        std::string error;
        if (openVideo(path, error)) c.media.videoPath = path;
        else c.media.error = error;
    }
    float height = 0.0f;
    if (c.media.videoPath == path) height = drawPicture(c, videoTexture(), at, width) + 8 * c.s;
    const bool playing = c.media.videoPath == path && videoPlaying();
    if (pageButton(c.draw, ImVec2(at.x, at.y + height), playing ? "Stop" : "Play", playing, c.s)){
        if (playing){
            closeVideo();
            c.media.videoPath.clear();
        } else playVideo();
    }
    if (playing)
        c.draw->AddText(uiFonts().mono, 14 * c.s, ImVec2(at.x + 100 * c.s, at.y + height + 8 * c.s), uiColor(UiColor::Dim), timeText(videoPosition(), videoLength()).c_str());
    height += 32 * c.s;
    return height + drawCaption(c, block, ImVec2(at.x, at.y + height), width);
}

// What a scored block runs, said plainly: "Reading drill", "Play this note"...
std::string exerciseKind(const ExerciseFile& exercise){
    switch (exercise.type){
        case ExerciseType::Reading: return "Reading to a beat";
        case ExerciseType::Scale: return "Scale drill";
        case ExerciseType::Rhythm: return "Rhythm drill";
        case ExerciseType::Chords: return "Chord changes";
        case ExerciseType::Notes: return "Play the notes";
        case ExerciseType::Neck: return "Find it on the neck";
        case ExerciseType::NeckWalk: return "Game";
        case ExerciseType::Intervals: return "Intervals by ear";
        case ExerciseType::Singing: return "Sing it back";
        case ExerciseType::Fretboard: return "Fretboard quiz";
        default: return "Exercise";
    }
}

// A drill or a song to play: a card with its name, what it is and its goal; passed, it says so. Clicked, it starts.
float drawScoredBlock(const BlockContext& c, const LessonBlock& block, ImVec2 at, float width, int number, const PageState& state,
                      PageEvents& events){
    const float s = c.s, pad = 18 * s, height = 92 * s;
    const UiFonts& fonts = uiFonts();
    const bool passed = number < (int)state.passed.size() && state.passed[(size_t)number];
    const bool chosen = state.chosen == number;
    const ExerciseEntry* exercise = number < (int)state.exercises.size() ? state.exercises[(size_t)number] : nullptr;
    const ImVec2 b(at.x + width, at.y + height);
    const bool hovered = ImGui::IsMouseHoveringRect(at, b);
    c.draw->AddRectFilled(at, b, uiColor(chosen || hovered ? UiColor::Accent : UiColor::Card, chosen ? 0.14f : hovered ? 0.08f : 1.0f), 10 * s);
    c.draw->AddRect(at, b, uiColor(chosen ? UiColor::Accent : passed ? UiColor::Good : UiColor::StaffLine, chosen ? 1.0f : 0.8f), 10 * s, 0,
                    chosen ? 2 * s : 1.0f);
    // What it is, its name, its goal
    std::string kind, name = block.name;
    if (block.type == BlockType::Play){
        // "Play along · bars 5 to 8 · 80%"
        kind = "Play along";
        int from = 0, to = 0;
        std::istringstream(blockValue(block, "bars")) >> from >> to;
        if (from > 0) kind += from == to ? TextFormat("  ·  bar %d", from) : TextFormat("  ·  bars %d to %d", from, to);
        const std::string tempo = blockValue(block, "tempo");
        if (std::atoi(tempo.c_str()) < 100) kind += "  ·  " + tempo + "%";
        if (name.empty()) name = blockValue(block, "song").empty() ? fs::path(blockValue(block, "file")).stem().string() : blockValue(block, "song");
    } else if (block.type == BlockType::Practice){
        kind = blockValue(block, "from") == "lesson" ? "Practice  ·  this lesson's notes" : "Practice  ·  the notes you miss most";
        if (name.empty()) name = blockValue(block, "as") == "reading" ? "Read them to a beat" : "Play them";
    } else if (exercise){
        kind = exerciseKind(exercise->exercise);
        if (name.empty()) name = exercise->exercise.title;
    } else {
        kind = "Exercise";
        if (name.empty()) name = blockValue(block, "exercise").empty() ? "Choose an exercise" : blockValue(block, "exercise") + " (not found)";
    }
    if (!blockGates(block)) kind += "  ·  optional";
    c.draw->AddText(fonts.mono, 12 * s, ImVec2(at.x + pad, at.y + pad - 2 * s), uiColor(UiColor::Dim), kind.c_str());
    c.draw->AddText(fonts.bold, 20 * s, ImVec2(at.x + pad, at.y + pad + 14 * s), uiColor(UiColor::Ink), name.c_str(), nullptr, width - 2 * pad - 120 * s);
    c.draw->AddText(fonts.text, 15 * s, ImVec2(at.x + pad, at.y + pad + 42 * s),
                    uiColor(passed ? UiColor::Good : UiColor::Dim), passed ? "Passed" : scoredBlockGoalText(block, exercise).c_str());
    // At the right: a tick once passed, else the way in
    const char* go = passed ? "Again" : "Start";
    const ImVec2 goExtent = fonts.bold->CalcTextSizeA(16 * s, FLT_MAX, 0.0f, go);
    const ImVec2 goAt(b.x - pad - goExtent.x - 28 * s, at.y + (height - 34 * s) / 2);
    c.draw->AddRectFilled(goAt, ImVec2(b.x - pad, goAt.y + 34 * s), uiColor(passed ? UiColor::Card : UiColor::Accent), 17 * s);
    if (passed) c.draw->AddRect(goAt, ImVec2(b.x - pad, goAt.y + 34 * s), uiColor(UiColor::Good), 17 * s, 0, 1.5f * s);
    c.draw->AddText(fonts.bold, 16 * s, ImVec2(goAt.x + 14 * s, goAt.y + (34 * s - goExtent.y) / 2), uiColor(passed ? UiColor::Good : UiColor::Background), go);
    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) events.started = number;
    return height;
}

} // namespace

float drawLessonPage(const LessonDoc& doc, int pageNumber, const std::string& folder, PageMedia& media, const PageState& state,
                     PageEvents& events, ImVec2 at, float width, float s){
    if (pageNumber < 0 || pageNumber >= (int)doc.pages.size()) return 0.0f;
    const LessonPage& page = doc.pages[(size_t)pageNumber];
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const BlockContext context{ draw, folder, media, doc.instrument, state.editing, s };
    float y = at.y;
    if (!page.title.empty()){
        draw->AddText(uiFonts().heavy, PAGE_TITLE_SIZE * s, ImVec2(at.x, y), uiColor(UiColor::Ink), page.title.c_str(), nullptr, width);
        y += textHeight(uiFonts().heavy, PAGE_TITLE_SIZE * s, width, page.title) + SECTION_GAP * s * 0.8f;
    }
    int scored = 0; // the page's scored blocks, counted in reading order
    if (page.sections.empty() && state.editing){
        y += drawPlaceholder(draw, ImVec2(at.x, y), width, 90 * s, "An empty page: add a block from the left, or drop one here", s);
        events.columns.push_back({ -1, -1, -1, ImVec2(at.x, y - 90 * s), ImVec2(at.x + width, y) });
    }
    for (size_t sectionIndex = 0; sectionIndex < page.sections.size(); sectionIndex++){
        const LessonSection& section = page.sections[sectionIndex];
        if (sectionIndex > 0) y += SECTION_GAP * s;
        const std::vector<float> shares = sectionShares(section.layout);
        const float usable = width - COLUMN_GAP * s * (float)(shares.size() - 1);
        float x = at.x, tallest = 0.0f;
        const size_t firstColumn = events.columns.size();
        for (size_t c = 0; c < section.columns.size() && c < shares.size(); c++){
            const float columnWidth = usable * shares[c];
            float columnY = y;
            if (section.columns[c].empty() && state.editing) // somewhere to put the first block
                columnY += drawPlaceholder(draw, ImVec2(x, y), columnWidth, 64 * s, "An empty column: add or drop a block here", s);
            for (size_t b = 0; b < section.columns[c].size(); b++){
                const LessonBlock& block = section.columns[c][b];
                if (b > 0) columnY += BLOCK_GAP * s;
                const ImVec2 blockAt(x, columnY);
                switch (block.type){
                    case BlockType::Text: columnY += drawText(context, block, blockAt, columnWidth); break;
                    case BlockType::Heading: columnY += drawHeading(context, block, blockAt, columnWidth); break;
                    case BlockType::Callout: columnY += drawCallout(context, block, blockAt, columnWidth); break;
                    case BlockType::Reveal:
                        columnY += drawReveal(context, block, blockAt, columnWidth, TextFormat("%d.%d.%d", (int)sectionIndex, (int)c, (int)b));
                        break;
                    case BlockType::Fretboard: columnY += drawNeckBlock(context, block, blockAt, columnWidth); break;
                    case BlockType::Keyboard: columnY += drawKeyboardBlock(context, block, blockAt, columnWidth); break;
                    case BlockType::Staff: columnY += drawStaffBlock(context, block, blockAt, columnWidth); break;
                    case BlockType::Image: columnY += drawImageBlock(context, block, blockAt, columnWidth); break;
                    case BlockType::Audio: columnY += drawAudioBlock(context, block, blockAt, columnWidth); break;
                    case BlockType::Video: columnY += drawVideoBlock(context, block, blockAt, columnWidth); break;
                    case BlockType::Exercise:
                    case BlockType::Play:
                    case BlockType::Practice: {
                        const float tall = drawScoredBlock(context, block, blockAt, columnWidth, scored++, state, events);
                        events.scoredSpans.push_back(ImVec2(columnY, columnY + tall));
                        columnY += tall;
                        break;
                    }
                    case BlockType::Count: break;
                }
                // (a block that draws nothing still has a little room, to be found and chosen)
                columnY = std::max(columnY, blockAt.y + (state.editing ? 12 * s : 0.0f));
                events.blocks.push_back({ (int)sectionIndex, (int)c, (int)b, blockAt, ImVec2(x + columnWidth, columnY) });
            }
            tallest = std::max(tallest, columnY - y);
            events.columns.push_back({ (int)sectionIndex, (int)c, -1, ImVec2(x, y), ImVec2(x + columnWidth, columnY) });
            x += columnWidth + COLUMN_GAP * s;
        }
        // Each column as tall as its section: somewhere to drop under its last block
        for (size_t k = firstColumn; k < events.columns.size(); k++) events.columns[k].max.y = y + tallest;
        events.sections.push_back({ (int)sectionIndex, -1, -1, ImVec2(at.x, y), ImVec2(at.x + width, y + tallest) });
        y += tallest;
    }
    if (!media.error.empty()){
        y += 10 * s;
        draw->AddText(uiFonts().text, 15 * s, ImVec2(at.x, y), uiColor(UiColor::Bad), media.error.c_str(), nullptr, width);
        y += textHeight(uiFonts().text, 15 * s, width, media.error);
    }
    return y - at.y;
}
