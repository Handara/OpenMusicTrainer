#include "learn/lessonplayer.h"

#include "core/chart.h"
#include "app/playerprogress.h"
#include "core/profile.h"
#include "imgui.h"
#include "learn/playexercise.h"
#include "raylib.h"
#include "ui/menulist.h"
#include "ui/theme.h"
#include "ui/ui.h"

#include <algorithm>
#include <cfloat>
#include <cstdlib>
#include <filesystem>
#include <random>
#include <sstream>

const float MAX_PAGE_WIDTH = 1060.0f; // at a 720-pixel-tall window: lines of text stay easy to read
const float SCROLL_STEP = 60.0f;
const double BACK_AFTER_S = 1.6;      // a goal met: the moment to see it (and hear the crowd) before the lesson comes back

LessonPlayer::LessonPlayer(const LessonEntry& entry, std::map<int, ExerciseEntry> exercises, ExerciseFactory create,
                           const GameplayOptions& playOptions, std::vector<std::string> songFolders, const std::string& progressPath,
                           int startPage)
    : doc(entry.doc), folder(entry.folder), exercises(std::move(exercises)), create(create), playOptions(playOptions),
      songFolders(std::move(songFolders)), progressPath(progressPath){
    progress = loadLessonProgress(progressPath);
    upgradeLessonProgress(progress, entry.version); // a version 1 lesson's steps are its pages now
    // Reopens where the student was; a finished lesson starts over from the top
    page = progress.completed ? 0 : std::clamp(progress.reached, 0, std::max(0, (int)doc.pages.size() - 1));
    if (startPage >= 0) page = std::clamp(startPage, 0, std::max(0, (int)doc.pages.size() - 1));
    if (startPage < 0 && !doc.pages.empty() && doc.pages[(size_t)page].aside){ // help, shown only when sent there
        const int on = nextPage(page, 1);
        page = on >= 0 ? on : std::max(0, nextPage(page, -1));
    }
}

int LessonPlayer::nextPage(int from, int by) const {
    for (int p = from + by; p >= 0 && p < (int)doc.pages.size(); p += by) if (!doc.pages[(size_t)p].aside) return p;
    return -1;
}

void LessonPlayer::sendTo(int to){
    if (to < 0 || to >= (int)doc.pages.size()) return;
    const int from = page;
    goTo(to);
    helpFrom = doc.pages[(size_t)to].aside ? from : -1; // help: Next comes back
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
    const int key = scoredBlockKey(doc, place);
    auto found = exercises.find(key);
    if (found != exercises.end()) return &found->second;
    auto practice = made.find(key);
    return practice == made.end() ? nullptr : &practice->second;
}

int LessonPlayer::goal(const BlockPlace& place) const {
    const LessonBlock& block = blockAt(doc, place);
    const ExerciseEntry* exercise = exerciseFor(place);
    ExerciseType type = exercise ? exercise->exercise.type : ExerciseType::Intervals;
    if (block.type == BlockType::Practice) type = blockValue(block, "as") == "reading" ? ExerciseType::Reading : ExerciseType::Notes;
    return scoredBlockGoal(block, type);
}

void LessonPlayer::save(){
    saveError.clear();
    if (!saveLessonProgress(progressPath, progress, saveError)) TraceLog(LOG_WARNING, "Progress: %s", saveError.c_str());
}

void LessonPlayer::goTo(int index){
    stopRunning();
    helpFrom = -1;
    sendPage = -1;
    releasePageMedia(media); // the old page's pictures, sound, video
    page = std::clamp(index, 0, std::max(0, (int)doc.pages.size() - 1));
    scroll = 0.0f;
    chosen = -1;
    progress.reached = std::max(progress.reached, page);
    save();
}

void LessonPlayer::goOn(){
    stopRunning();
    if (helpFrom >= 0){ // help seen: back to where it was needed
        const int back = helpFrom;
        goTo(back);
        return;
    }
    const int next = nextPage(page, 1);
    if (next < 0){
        progress.completed = true;
        save();
        leave = true;
        return;
    }
    goTo(next);
}

void LessonPlayer::start(int number){
    const std::vector<BlockPlace> blocks = pageBlocks();
    if (number < 0 || number >= (int)blocks.size()) return;
    releasePageMedia(media); // what runs may need the audio a sound or video holds
    const BlockPlace place = blocks[(size_t)number];
    const LessonBlock& block = blockAt(doc, place);
    if (block.type == BlockType::Play){
        // Its part (a bass's listened to as a bass); a few bars of it, or slower, played as in practice mode, once
        const std::string chartPath = songBlockChart(block, folder, songFolders);
        GameplayOptions options = playOptions;
        options.part = std::max(0, std::atoi(blockValue(block, "part").c_str()) - 1);
        Chart chart;
        std::string error;
        if (loadChart(chartPath, chart, error)){
            if (options.part < (int)chart.frettedTracks.size())
                options.instrument = chart.frettedTracks[(size_t)options.part].type == InstrumentType::Bass ? InputRole::Bass : InputRole::Guitar;
            int from = 0, to = 0;
            std::istringstream(blockValue(block, "bars")) >> from >> to;
            const int tempo = std::atoi(blockValue(block, "tempo").c_str());
            if (from > 0 || tempo < 100){
                options.practice.on = true;
                options.practice.fromTick = from > 0 ? barStartTick(chart, from - 1) : 0;
                options.practice.toTick = from > 0 ? std::min(chart.endTick, barStartTick(chart, to)) : chart.endTick;
                options.practice.speed = std::clamp(tempo, 30, 100) / 100.0f;
                options.practice.passes = 1;
            }
        }
        running = std::make_unique<PlayExercise>(chartPath, options);
    }
    else if (block.type == BlockType::Practice){
        // Its notes: the student's weakest lately on the lesson's instrument (enough of them), else the lesson's own
        const int count = std::max(2, std::atoi(blockValue(block, "count").c_str()));
        const char* instrument = doc.instrument == ExerciseInstrument::Bass ? "bass" : doc.instrument == ExerciseInstrument::Piano ? "piano"
                               : doc.instrument == ExerciseInstrument::Guitar ? "guitar" : "";
        std::vector<int> pitches;
        if (blockValue(block, "from") == "weak")
            for (const NoteTally& note : weakestNotes(playerProfile(), count, 3, instrument)) pitches.push_back(note.pitch);
        if (pitches.size() < 2){
            pitches = lessonNotes(doc);
            std::shuffle(pitches.begin(), pitches.end(), std::mt19937(std::random_device{}()));
            if ((int)pitches.size() > count) pitches.resize((size_t)count);
        }
        if (pitches.size() < 2) // nothing yet to go by: the first notes there are
            pitches = doc.instrument == ExerciseInstrument::Bass ? std::vector<int>{ 28, 29, 31 }
                    : doc.instrument == ExerciseInstrument::Piano ? std::vector<int>{ 60, 62, 64 } : std::vector<int>{ 64, 65, 67 };
        std::sort(pitches.begin(), pitches.end());
        ExerciseEntry entry;
        entry.id = entry.name = "lesson-practice-" + std::filesystem::path(folder).filename().string() + "-" + std::to_string(scoredBlockKey(doc, place));
        entry.builtIn = false;
        std::string error;
        if (parseExercise(practiceExercise(block, pitches, doc.instrument), "practice", 1, block.name.empty() ? "Practice" : block.name,
                          entry.exercise, error)){
            made[scoredBlockKey(doc, place)] = entry;
            running = create(entry);
        }
    }
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
        // Each run played to its end: missed twice in a row, a block may send the student to help; aced the first time,
        // on ahead
        int percent = 0;
        if (running->takeFinishedRun(percent)){
            const int key = scoredBlockKey(doc, runningPlace);
            const LessonBlock& block = blockAt(doc, runningPlace);
            runs[key]++;
            if (passed(runningPlace)){
                misses[key] = 0;
                const int ahead = findPage(doc, blockValue(block, "ace"));
                if (runs[key] == 1 && percent >= 100 && ahead >= 0){
                    sendPage = ahead;
                    sendAt = GetTime() + BACK_AFTER_S;
                }
            } else if (++misses[key] >= 2){
                const int help = findPage(doc, blockValue(block, "help"));
                if (help >= 0){
                    misses[key] = 0;
                    sendPage = help;
                    sendAt = GetTime() + BACK_AFTER_S;
                }
            }
        }
        if (sendPage >= 0 && GetTime() >= sendAt){
            const int to = sendPage;
            sendTo(to);
            return;
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
    if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow, false)){
        if (helpFrom >= 0) goTo(helpFrom);
        else if (nextPage(page, -1) >= 0) goTo(nextPage(page, -1));
    }
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
    // (in the middle: the corners are the Back button's and the instrument's legend's). The pages on the way through
    // as dots; on a page of help, a word saying so
    std::vector<int> shown;
    for (int p = 0; p < (int)doc.pages.size(); p++) if (!doc.pages[(size_t)p].aside) shown.push_back(p);
    const bool help = doc.pages[(size_t)page].aside;
    const int count = help ? 1 : (int)shown.size();
    const float spacing = std::min(22 * s, width * 0.3f / std::max(1, count)), dotsWidth = help ? 60 * s : spacing * (count - 1);
    const float y = height * 0.835f, left = width / 2 - dotsWidth / 2 - 40 * s, right = width / 2 + dotsWidth / 2 + 40 * s;
    const float dotsLeft = width / 2 - dotsWidth / 2;
    if (help){
        const ImVec2 extent = fonts.mono->CalcTextSizeA(13 * s, FLT_MAX, 0.0f, "HELP");
        draw->AddText(fonts.mono, 13 * s, ImVec2(width / 2 - extent.x / 2, y + 17 * s - extent.y / 2), uiColor(UiColor::Accent), "HELP");
    } else for (int i = 0; i < count; i++){
        const ImVec2 c(dotsLeft + i * spacing, y + 17 * s);
        const int p = shown[(size_t)i];
        if (p == page) draw->AddCircleFilled(c, 6 * s, uiColor(UiColor::Accent));
        else if (p <= progress.reached) draw->AddCircleFilled(c, 4.5f * s, uiColor(UiColor::Dim));
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
    const int before = helpFrom >= 0 ? helpFrom : nextPage(page, -1);
    if (before >= 0 && button(left, true, "Back", false, true)) goTo(before); // ending left of the dots
    const bool last = helpFrom < 0 && nextPage(page, 1) < 0;
    const bool open = canGoOn();
    const char* next = helpFrom >= 0 ? "Back to it" : last ? "Finish" : "Next";
    if (button(right, false, next, true, open)) goOn(); // starting right of them
    if (!open){
        const char* held = "Pass the drill to go on";
        const ImVec2 extent = fonts.text->CalcTextSizeA(14 * s, FLT_MAX, 0.0f, held);
        const float nextWidth = fonts.bold->CalcTextSizeA(17 * s, FLT_MAX, 0.0f, next).x + 40 * s;
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
