#include "learn/lessonpage.h"

#include "audio/audio.h"
#include "ui/theme.h"
#include "video/video.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <filesystem>

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
    float s;
};

float textHeight(ImFont* font, float size, float width, const std::string& text){
    return font->CalcTextSizeA(size, FLT_MAX, width, text.c_str()).y;
}

// Paragraphs, wrapped to the width, a little apart; returns their height
float drawParagraphs(ImDrawList* draw, ImFont* font, float size, ImVec2 at, float width, ImU32 color, const std::vector<std::string>& paragraphs, float s){
    float y = at.y;
    for (size_t i = 0; i < paragraphs.size(); i++){
        if (i > 0) y += PARAGRAPH_GAP * s;
        draw->AddText(font, size, ImVec2(at.x, y), color, paragraphs[i].c_str(), nullptr, width);
        y += textHeight(font, size, width, paragraphs[i]);
    }
    return y - at.y;
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
    return drawParagraphs(c.draw, uiFonts().text, TEXT_SIZE * c.s, at, width, uiColor(UiColor::Ink), blockValues(block, "text"), c.s);
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
    float textTall = 0.0f;
    const std::vector<std::string> paragraphs = blockValues(block, "text");
    for (size_t i = 0; i < paragraphs.size(); i++) textTall += (i ? PARAGRAPH_GAP * s : 0.0f) + textHeight(uiFonts().text, 17 * s, inner, paragraphs[i]);
    const float height = pad + 20 * s + textTall + pad;
    c.draw->AddRectFilled(at, ImVec2(at.x + width, at.y + height), uiColor(color, 0.08f), 8 * s);
    c.draw->AddRectFilled(at, ImVec2(at.x + bar, at.y + height), uiColor(color), 8 * s, ImDrawFlags_RoundCornersLeft);
    c.draw->AddText(uiFonts().mono, 13 * s, ImVec2(at.x + bar + pad, at.y + pad), uiColor(color), label);
    drawParagraphs(c.draw, uiFonts().text, 17 * s, ImVec2(at.x + bar + pad, at.y + pad + 20 * s), inner, uiColor(UiColor::Ink), paragraphs, s);
    return height;
}

float drawImageBlock(const BlockContext& c, const LessonBlock& block, ImVec2 at, float width){
    const Texture2D* texture = pictureFor(c.media, (fs::path(c.folder) / blockValue(block, "file")).string());
    float height = texture ? drawPicture(c, *texture, at, width) : 0.0f;
    return height + drawCaption(c, block, ImVec2(at.x, at.y + height), width);
}

// A sound: Listen (or Stop) and where it's got to; through the song stream, so starting it stops any other
float drawAudioBlock(const BlockContext& c, const LessonBlock& block, ImVec2 at, float width){
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
        kind = "Play along";
        if (name.empty()) name = fs::path(blockValue(block, "file")).stem().string();
    } else if (exercise){
        kind = exerciseKind(exercise->exercise);
        if (name.empty()) name = exercise->exercise.title;
    } else {
        kind = "Exercise";
        if (name.empty()) name = blockValue(block, "exercise") + " (not found)";
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
    const BlockContext context{ draw, folder, media, s };
    float y = at.y;
    if (!page.title.empty()){
        draw->AddText(uiFonts().heavy, PAGE_TITLE_SIZE * s, ImVec2(at.x, y), uiColor(UiColor::Ink), page.title.c_str(), nullptr, width);
        y += textHeight(uiFonts().heavy, PAGE_TITLE_SIZE * s, width, page.title) + SECTION_GAP * s * 0.8f;
    }
    int scored = 0; // the page's scored blocks, counted in reading order
    for (size_t sectionIndex = 0; sectionIndex < page.sections.size(); sectionIndex++){
        const LessonSection& section = page.sections[sectionIndex];
        if (sectionIndex > 0) y += SECTION_GAP * s;
        const std::vector<float> shares = sectionShares(section.layout);
        const float usable = width - COLUMN_GAP * s * (float)(shares.size() - 1);
        float x = at.x, tallest = 0.0f;
        for (size_t c = 0; c < section.columns.size() && c < shares.size(); c++){
            const float columnWidth = usable * shares[c];
            float columnY = y;
            for (size_t b = 0; b < section.columns[c].size(); b++){
                const LessonBlock& block = section.columns[c][b];
                if (b > 0) columnY += BLOCK_GAP * s;
                const ImVec2 blockAt(x, columnY);
                switch (block.type){
                    case BlockType::Text: columnY += drawText(context, block, blockAt, columnWidth); break;
                    case BlockType::Heading: columnY += drawHeading(context, block, blockAt, columnWidth); break;
                    case BlockType::Callout: columnY += drawCallout(context, block, blockAt, columnWidth); break;
                    case BlockType::Image: columnY += drawImageBlock(context, block, blockAt, columnWidth); break;
                    case BlockType::Audio: columnY += drawAudioBlock(context, block, blockAt, columnWidth); break;
                    case BlockType::Video: columnY += drawVideoBlock(context, block, blockAt, columnWidth); break;
                    case BlockType::Exercise:
                    case BlockType::Play: {
                        const float tall = drawScoredBlock(context, block, blockAt, columnWidth, scored++, state, events);
                        events.scoredSpans.push_back(ImVec2(columnY, columnY + tall));
                        columnY += tall;
                        break;
                    }
                    case BlockType::Count: break;
                }
            }
            tallest = std::max(tallest, columnY - y);
            x += columnWidth + COLUMN_GAP * s;
        }
        y += tallest;
    }
    if (!media.error.empty()){
        y += 10 * s;
        draw->AddText(uiFonts().text, 15 * s, ImVec2(at.x, y), uiColor(UiColor::Bad), media.error.c_str(), nullptr, width);
        y += textHeight(uiFonts().text, 15 * s, width, media.error);
    }
    return y - at.y;
}
