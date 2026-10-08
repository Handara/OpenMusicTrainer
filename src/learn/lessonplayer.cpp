#include "learn/lessonplayer.h"

#include "imgui.h"
#include "learn/playexercise.h"
#include "raylib.h"
#include "ui/menulist.h"
#include "ui/theme.h"
#include "ui/ui.h"

#include <algorithm>
#include <cfloat>
#include <filesystem>

const float MAX_PAGE_WIDTH = 1060.0f; // at a 720-pixel-tall window: lines of text stay easy to read
const float SCROLL_STEP = 60.0f;
const double BACK_AFTER_S = 1.6;      // a goal met: the moment to see it (and hear the crowd) before the lesson comes back

LessonPlayer::LessonPlayer(const LessonEntry& entry, std::map<int, ExerciseEntry> exercises, ExerciseFactory create,
                           const GameplayOptions& playOptions, const std::string& progressPath, int startPage)
    : doc(entry.doc), folder(entry.folder), exercises(std::move(exercises)), create(create), playOptions(playOptions),
      progressPath(progressPath){
    progress = loadLessonProgress(progressPath);
    upgradeLessonProgress(progress, entry.version); // a version 1 lesson's steps are its pages now
    // Reopens where the student was; a finished lesson starts over from the top
    page = progress.completed ? 0 : std::clamp(progress.reached, 0, std::max(0, (int)doc.pages.size() - 1));
    if (startPage >= 0) page = std::clamp(startPage, 0, std::max(0, (int)doc.pages.size() - 1));
}

LessonPlayer::~LessonPlayer(){
    running.reset();
    releasePageMedia(media);
}

std::vector<BlockPlace> LessonPlayer::pageBlocks() const {
    return scoredBlocks(doc, page);
}

bool LessonPlayer::passed(const BlockPlace& place) const {
    return stepPassed(progress, scoredBlockKey(doc, place));
}

int LessonPlayer::firstToPass() const {
    const std::vector<BlockPlace> blocks = pageBlocks();
    for (int i = 0; i < (int)blocks.size(); i++)
        if (blockGates(blockAt(doc, blocks[(size_t)i])) && !passed(blocks[(size_t)i])) return i;
    return -1;
}

bool LessonPlayer::canGoOn() const {
    return firstToPass() < 0;
}

const ExerciseEntry* LessonPlayer::exerciseFor(const BlockPlace& place) const {
    auto found = exercises.find(scoredBlockKey(doc, place));
    return found == exercises.end() ? nullptr : &found->second;
}

int LessonPlayer::goal(const BlockPlace& place) const {
    const ExerciseEntry* exercise = exerciseFor(place);
    return scoredBlockGoal(blockAt(doc, place), exercise ? exercise->exercise.type : ExerciseType::Intervals);
}

void LessonPlayer::save(){
    saveError.clear();
    if (!saveLessonProgress(progressPath, progress, saveError)) TraceLog(LOG_WARNING, "Progress: %s", saveError.c_str());
}

void LessonPlayer::goTo(int index){
    stopRunning();
    releasePageMedia(media); // the old page's pictures, sound, video
    page = std::clamp(index, 0, std::max(0, (int)doc.pages.size() - 1));
    scroll = 0.0f;
    chosen = -1;
    progress.reached = std::max(progress.reached, page);
    save();
}

void LessonPlayer::goOn(){
    stopRunning();
    if (page + 1 >= (int)doc.pages.size()){
        progress.completed = true;
        save();
        leave = true;
        return;
    }
    goTo(page + 1);
}

void LessonPlayer::start(int number){
    const std::vector<BlockPlace> blocks = pageBlocks();
    if (number < 0 || number >= (int)blocks.size()) return;
    releasePageMedia(media); // what runs may need the audio a sound or video holds
    const BlockPlace place = blocks[(size_t)number];
    const LessonBlock& block = blockAt(doc, place);
    if (block.type == BlockType::Play)
        running = std::make_unique<PlayExercise>((std::filesystem::path(folder) / blockValue(block, "file")).string(), playOptions);
    else if (const ExerciseEntry* exercise = exerciseFor(place)) running = create(*exercise);
    runningPlace = place;
    chosen = number;
    passedAt = -1.0;
}

void LessonPlayer::stopRunning(){
    running.reset(); // its destructor puts back what it changed (the microphone, keyboard navigation)
    passedAt = -1.0;
}

void LessonPlayer::update(){
    if (running){
        running->update();
        if (!passed(runningPlace) && running->lessonScore() >= goal(runningPlace)){
            passStep(progress, scoredBlockKey(doc, runningPlace));
            save();
            passedAt = GetTime();
        }
        if (running->wantsToLeave()) stopRunning();
        else if (passedAt >= 0.0 && GetTime() - passedAt >= BACK_AFTER_S){
            // A page that's only this block goes on; else back to the page, its card ticked
            const LessonPage& shown = doc.pages[(size_t)page];
            const bool alone = shown.sections.size() == 1 && shown.sections[0].columns.size() == 1 && shown.sections[0].columns[0].size() == 1;
            stopRunning();
            if (alone && canGoOn()) goOn();
        }
        return;
    }
    const std::vector<BlockPlace> blocks = pageBlocks();
    // Up and Down choose a drill on the page (or scroll a page without one); Enter starts it, else the first one still
    // holding the page, else turns the page
    if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)){
        if (!blocks.empty()) chosen = std::min((int)blocks.size() - 1, chosen + 1), chosenMoved = true;
        else scroll += SCROLL_STEP * menuScale();
    }
    if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)){
        if (!blocks.empty()) chosen = std::max(-1, chosen - 1), chosenMoved = true;
        else scroll -= SCROLL_STEP * menuScale();
        if (chosen < 0) scroll = 0.0f; // back above them all: the top of the page
    }
    scroll -= ImGui::GetIO().MouseWheel * SCROLL_STEP * menuScale();
    if (ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false)){
        if (chosen >= 0) start(chosen);
        else if (firstToPass() >= 0) start(firstToPass());
        else goOn();
        return;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_RightArrow, false)){
        if (canGoOn()) goOn();
        else start(firstToPass());
        return;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow, false) && page > 0) goTo(page - 1);
}

bool LessonPlayer::back(){
    if (!running) return false;
    stopRunning();
    return true;
}

// While a block runs: a line at the top, centred, with its goal (or that it's met) and the way back
void LessonPlayer::drawGoalBar(float s){
    ImDrawList* draw = ImGui::GetForegroundDrawList();
    const UiFonts& fonts = uiFonts();
    const bool done = passed(runningPlace);
    const ExerciseEntry* exercise = exerciseFor(runningPlace);
    const std::string text = done ? "Goal reached: back to the lesson in a moment"
                                  : scoredBlockGoalText(blockAt(doc, runningPlace), exercise) + TextFormat("  (now %d)    Esc: back to the lesson",
                                                                                                          running->lessonScore());
    const ImVec2 extent = fonts.bold->CalcTextSizeA(15 * s, FLT_MAX, 0.0f, text.c_str());
    const float width = ImGui::GetIO().DisplaySize.x, x = (width - extent.x) / 2 - 16 * s, y = 14 * s;
    draw->AddRectFilled(ImVec2(x, y), ImVec2(x + extent.x + 32 * s, y + 30 * s), uiColor(done ? UiColor::Good : UiColor::Card, done ? 0.9f : 0.95f), 15 * s);
    draw->AddText(fonts.bold, 15 * s, ImVec2(x + 16 * s, y + (30 * s - extent.y) / 2), uiColor(done ? UiColor::Background : UiColor::Dim), text.c_str());
}

// At the foot: Back at the left, the pages as dots in the middle, Next (Finish on the last page) at the right
void LessonPlayer::drawFoot(float s){
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const UiFonts& fonts = uiFonts();
    const float width = ImGui::GetWindowWidth(), height = ImGui::GetWindowHeight();
    // (in the middle: the corners are the Back button's and the instrument's legend's)
    const int count = (int)doc.pages.size();
    const float spacing = std::min(22 * s, width * 0.3f / std::max(1, count)), dotsWidth = spacing * (count - 1);
    const float y = height * 0.835f, left = width / 2 - dotsWidth / 2 - 40 * s, right = width / 2 + dotsWidth / 2 + 40 * s;
    const float dotsLeft = width / 2 - dotsWidth / 2;
    for (int i = 0; i < count; i++){
        const ImVec2 c(dotsLeft + i * spacing, y + 17 * s);
        if (i == page) draw->AddCircleFilled(c, 6 * s, uiColor(UiColor::Accent));
        else if (i <= progress.reached) draw->AddCircleFilled(c, 4.5f * s, uiColor(UiColor::Dim));
        else draw->AddCircle(c, 4.5f * s, uiColor(UiColor::Dim, 0.5f), 0, 1.5f * s);
    }
    auto button = [&](float x, bool alignRight, const char* label, bool lit, bool live){
        const ImVec2 extent = fonts.bold->CalcTextSizeA(17 * s, FLT_MAX, 0.0f, label);
        const float w = extent.x + 40 * s;
        const ImVec2 a(alignRight ? x - w : x, y), b(a.x + w, y + 34 * s);
        const bool hovered = live && ImGui::IsMouseHoveringRect(a, b);
        draw->AddRectFilled(a, b, uiColor(lit && live ? UiColor::Accent : UiColor::Card, live ? 1.0f : 0.5f), 17 * s);
        if (!lit || !live) draw->AddRect(a, b, uiColor(hovered ? UiColor::Accent : UiColor::StaffLine), 17 * s, 0, 1.0f);
        draw->AddText(fonts.bold, 17 * s, ImVec2(a.x + 20 * s, y + (34 * s - extent.y) / 2),
                      uiColor(lit && live ? UiColor::Background : live ? UiColor::Ink : UiColor::Dim), label);
        return hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left);
    };
    if (page > 0 && button(left, true, "Back", false, true)) goTo(page - 1); // ending left of the dots
    const bool last = page + 1 == count;
    const bool open = canGoOn();
    if (button(right, false, last ? "Finish" : "Next", true, open)) goOn(); // starting right of them
    if (!open){
        const char* held = "Pass the drill to go on";
        const ImVec2 extent = fonts.text->CalcTextSizeA(14 * s, FLT_MAX, 0.0f, held);
        const float nextWidth = fonts.bold->CalcTextSizeA(17 * s, FLT_MAX, 0.0f, last ? "Finish" : "Next").x + 40 * s;
        draw->AddText(fonts.text, 14 * s, ImVec2(right + nextWidth + 14 * s, y + (34 * s - extent.y) / 2), uiColor(UiColor::Dim), held); // beside Next
    }
}

void LessonPlayer::draw(){
    const float s = menuScale();
    if (running){
        running->draw();
        drawGoalBar(s);
        return;
    }
    menuTitle(doc.title.c_str());
    const float width = ImGui::GetWindowWidth(), height = ImGui::GetWindowHeight();
    const float pageWidth = std::min(width * 0.86f, MAX_PAGE_WIDTH * s);
    // (its foot clear of the corner where the instrument's legend is)
    const float left = (width - pageWidth) / 2, top = height * 0.18f, bottom = height * 0.79f;
    // The page, scrolled when it's taller than its room
    scroll = std::clamp(scroll, 0.0f, std::max(0.0f, pageHeight - (bottom - top)));
    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->PushClipRect(ImVec2(0, top - 4 * s), ImVec2(width, bottom), true);
    PageState state;
    const std::vector<BlockPlace> blocks = pageBlocks();
    for (const BlockPlace& place : blocks){
        state.passed.push_back(passed(place));
        state.exercises.push_back(exerciseFor(place));
    }
    state.chosen = chosen;
    PageEvents events;
    pageHeight = drawLessonPage(doc, page, folder, media, state, events, ImVec2(left, top - scroll), pageWidth, s);
    draw->PopClipRect();
    // The block just chosen with the keys, scrolled into view (from the next frame)
    if (chosenMoved && chosen >= 0 && chosen < (int)events.scoredSpans.size()){
        const ImVec2 span = events.scoredSpans[(size_t)chosen];
        if (span.y > bottom - 12 * s) scroll += span.y - (bottom - 12 * s);
        if (span.x < top) scroll -= top - span.x;
    }
    chosenMoved = false;
    if (pageHeight > bottom - top){ // more below (or above): a fade at that edge
        if (scroll < pageHeight - (bottom - top) - 1.0f)
            draw->AddRectFilledMultiColor(ImVec2(0, bottom - 30 * s), ImVec2(width, bottom), uiColor(UiColor::Background, 0.0f),
                                          uiColor(UiColor::Background, 0.0f), uiColor(UiColor::Background), uiColor(UiColor::Background));
    }
    if (events.started >= 0){
        start(events.started);
        return;
    }
    drawFoot(s);
    if (!saveError.empty()) centeredErrorText("Progress could not be saved: " + saveError);
    menuScreenHint(blocks.empty() ? "Left / Right  pages    Up / Down  scroll    Esc  back"
                                  : "Left / Right  pages    Up / Down  choose    Enter  start    Esc  back", s);
    ImGui::Dummy(ImVec2(1, 1));
}
