#include "learn/coursechapter.h"

#include "app/playerprogress.h"
#include "imgui.h"
#include "ui/rewards.h"
#include "raylib.h"
#include "ui/menulist.h"
#include "ui/theme.h"
#include "ui/ui.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <filesystem>

const double NEXT_AFTER_S = 1.8;   // a drill passed: the moment to see it before the next starts
const float BANNER_S = 3.0f;

CourseChapter::CourseChapter(const Course& course, int lesson, std::function<std::vector<ExerciseEntry>(int)> drillsFor,
                             ExerciseFactory create, const std::string& scoresPath, int* shownLesson)
    : course(course), drillsFor(std::move(drillsFor)), shownLesson(shownLesson), create(create), scoresPath(scoresPath){
    scores = loadCourseScores(scoresPath);
    load(lesson);
    ImGui::GetIO().ConfigFlags &= ~ImGuiConfigFlags_NavEnableKeyboard; // the arrows and Enter choose here
}

void CourseChapter::load(int index){
    lesson = std::clamp(index, 0, (int)course.lessons.size() - 1);
    drills = courseDrills(course, lesson);
    entries = drillsFor(lesson);
    if (shownLesson) *shownLesson = lesson;
    // Words only: read, so passed
    if (drills.empty() && recordCourseScore(scores, course.lessons[lesson].id + "-read", 100)){
        std::string error;
        if (!saveCourseScores(scoresPath, scores, error)) TraceLog(LOG_WARNING, "Progress: %s", error.c_str());
    }
    const int next = firstNotPassed();
    chosen = next >= 0 ? next : hasNext() ? (int)drills.size() : 0; // all passed: the next chapter, chosen
    scroll = 0.0f;
    wordsScroll = 0.0f;
    releasePageMedia(media); // the last chapter's sounds and pictures
    chapterPassedAt = -100.0;
}

bool CourseChapter::hasNext() const {
    return lesson + 1 < (int)course.lessons.size() && chapterState(course, lesson, scores).passed;
}

CourseChapter::~CourseChapter(){
    running.reset();
    releasePageMedia(media);
    ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
}

int CourseChapter::firstNotPassed() const {
    for (int i = 0; i < (int)drills.size(); i++){
        if (drills[i].optional) continue; // a challenge: there to take, not to go through
        auto found = scores.best.find(drills[i].id);
        if (found == scores.best.end() || found->second < drills[i].passPercent) return i;
    }
    return -1;
}

void CourseChapter::startDrill(int index){
    if (index < 0 || index >= (int)drills.size()) return;
    running.reset();
    releasePageMedia(media); // what runs may need the audio a sound in the words holds
    runningIndex = index;
    chosen = index;
    passedAt = -1.0;
    lastPercent = -1;
    running = create(entries[index]);
    if (running && running->hasEndMenu()) running->offerNext(nextLabel()); // there from the first run, scored or not
}

void CourseChapter::stopDrill(){
    running.reset(); // its destructor puts back what it changed (microphone, keyboard navigation)
    runningIndex = -1;
    passedAt = -1.0;
    ImGui::GetIO().ConfigFlags &= ~ImGuiConfigFlags_NavEnableKeyboard; // an exercise gives keyboard navigation back
}

bool CourseChapter::back(){
    if (!running) return false;
    stopDrill();
    return true;
}

// A run of the running drill, scored: its best kept, and passed, on to the next
void CourseChapter::scored(int percent){
    const CourseDrill& drill = drills[runningIndex];
    const bool wasPassed = chapterState(course, lesson, scores).passed;
    lastPercent = percent;
    lastWasBest = recordCourseScore(scores, drill.id, percent);
    if (lastWasBest){
        std::string error;
        if (!saveCourseScores(scoresPath, scores, error)) TraceLog(LOG_WARNING, "Progress: %s", error.c_str());
    }
    if (percent >= drill.passPercent) passedAt = GetTime();
    if (!wasPassed && chapterState(course, lesson, scores).passed){
        chapterPassedAt = GetTime();
        // In the player's journal: the chapter, and its level or the whole course if it completes them
        Activity activity;
        activity.kind = ActivityKind::Chapter;
        activity.id = std::filesystem::path(scoresPath).stem().string() + "-" + course.lessons[lesson].id;
        activity.title = course.lessons[lesson].lesson.title;
        const ChapterState passedState = chapterState(course, lesson, scores);
        activity.right = passedState.stars; // a chapter's right and total: its stars
        activity.total = passedState.starsPossible;
        const CourseUnit& unit = course.units[course.lessons[lesson].unit];
        activity.unitDone = true;
        for (int i = unit.firstLesson; i < unit.firstLesson + unit.lessonCount; i++)
            activity.unitDone = activity.unitDone && chapterState(course, i, scores).passed;
        activity.courseDone = true;
        for (int i = 0; i < (int)course.lessons.size(); i++) activity.courseDone = activity.courseDone && chapterState(course, i, scores).passed;
        recordActivity(activity);
    }
    if (running && running->hasEndMenu()) running->offerNext(nextLabel());
}

// The drill after the running one; after the last, the next chapter once this one's passed (and whether it starts a
// new level)
std::string CourseChapter::nextLabel() const {
    if (runningIndex + 1 < (int)drills.size()) return "Next drill: " + drills[runningIndex + 1].name;
    if (!hasNext()) return "";
    const CourseLesson& next = course.lessons[lesson + 1];
    return "Next chapter: " + next.lesson.title + (next.unit != course.lessons[lesson].unit ? TextFormat("  (level %d)", next.unit + 1) : "");
}

void CourseChapter::update(){
    if (running){
        running->update();
        int percent = 0;
        if (running->takeFinishedRun(percent)) scored(percent);
        else if (!running->scoresRuns() && passedAt < 0.0){
            // An exercise that doesn't score runs: its lesson goal reached counts as all right
            const LessonBlock& block = blockAt(course.lessons[lesson].doc, drills[runningIndex].place);
            if (running->lessonScore() >= scoredBlockGoal(block, entries[runningIndex].exercise.type)) scored(100);
        }
        // Its end menu's "next": the next drill, or after the last, the next chapter's page
        if (running && running->takeNextChosen()){
            if (runningIndex + 1 < (int)drills.size()) startDrill(runningIndex + 1);
            else {
                stopDrill();
                if (hasNext()) load(lesson + 1);
            }
            return;
        }
        // Without one: on to the next drill by itself a moment after a pass, or, from a drill that gets harder as it
        // goes on (it's passed, and it keeps climbing), when the player says (Enter or N)
        const bool menu = running && running->hasEndMenu();
        const bool onward = menu ? false
                          : running && running->goesOn() ? passedAt >= 0.0 && (ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_N, false))
                                                         : passedAt >= 0.0 && GetTime() - passedAt >= NEXT_AFTER_S;
        if (running && running->wantsToLeave()) stopDrill();
        else if (onward){
            const int next = firstNotPassed();
            if (next >= 0 && next != runningIndex) startDrill(next);
            else {
                stopDrill(); // all passed (or this one again): back to the chapter, the next one chosen
                if (next < 0 && hasNext()) chosen = (int)drills.size();
            }
        }
        return;
    }
    // The drills, then (passed) the next chapter as one more row
    const int rows = (int)drills.size() + (hasNext() ? 1 : 0);
    if (rows == 0) return;
    if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) chosen = std::min(rows - 1, chosen + 1);
    if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) chosen = std::max(0, chosen - 1);
    chosen = std::clamp(chosen, 0, rows - 1);
    const bool confirm = ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false)
                         || ImGui::IsKeyPressed(ImGuiKey_Space, false);
    if ((confirm && chosen == (int)drills.size()) || (ImGui::IsKeyPressed(ImGuiKey_N, false) && hasNext())) load(lesson + 1);
    else if (confirm) startDrill(chosen);
}

// While a drill runs: a line at the top, which drill, its best, what passes it, and the last run
void CourseChapter::drawRunningBar(float s){
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const UiFonts& fonts = uiFonts();
    const CourseDrill& drill = drills[runningIndex];
    auto found = scores.best.find(drill.id);
    const int best = found == scores.best.end() ? 0 : found->second;
    std::string text = TextFormat("DRILL %d OF %d  ·  BEST %d%%  ·  %d%% TO PASS", runningIndex + 1, (int)drills.size(), best, drill.passPercent);
    if (lastPercent >= 0) text += TextFormat("  ·  THIS RUN %d%%%s", lastPercent, lastWasBest ? " (BEST!)" : "");
    if (passedAt >= 0.0 && running->goesOn() && !running->hasEndMenu()) text += "  ·  PASSED: KEEP GOING, OR ENTER FOR THE NEXT";
    else if (passedAt >= 0.0) text += "  ·  PASSED";
    const float width = ImGui::GetWindowWidth();
    const ImVec2 size = fonts.mono->CalcTextSizeA(13 * s, FLT_MAX, 0.0f, text.c_str());
    const bool passed = passedAt >= 0.0;
    draw->AddText(fonts.mono, 13 * s, ImVec2((width - size.x) / 2, 30 * s), uiColor(passed ? UiColor::Good : UiColor::Dim), text.c_str());
}

void CourseChapter::draw(){
    const float s = menuScale();
    if (running){
        running->draw();
        if (running) drawRunningBar(s);
        return;
    }
    const CourseLesson& chapter = course.lessons[lesson];
    menuTitle(chapter.lesson.title.c_str());
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const UiFonts& fonts = uiFonts();
    const float width = ImGui::GetWindowWidth(), height = ImGui::GetWindowHeight();
    const float left = width * 0.07f, right = width * 0.93f;
    const ChapterState state = chapterState(course, lesson, scores);
    int chapterNumber = lesson - course.units[chapter.unit].firstLesson + 1;
    draw->AddText(fonts.mono, 13 * s, ImVec2(left, height * 0.165f), uiColor(UiColor::Accent),
                  TextFormat("LEVEL %d  ·  CHAPTER %d  ·  %d%%%s", chapter.unit + 1, chapterNumber, state.percent, state.perfect ? "  ·  PERFECT" : ""));
    if (state.starsPossible > 0){ // its stars, beside
        const char* header = TextFormat("LEVEL %d  ·  CHAPTER %d  ·  %d%%%s", chapter.unit + 1, chapterNumber, state.percent, state.perfect ? "  ·  PERFECT" : "");
        const float after = left + fonts.mono->CalcTextSizeA(13 * s, FLT_MAX, 0.0f, header).x + 22 * s;
        drawStar(draw, ImVec2(after, height * 0.165f + 7 * s), 8 * s, uiColor(UiColor::Accent));
        draw->AddText(fonts.mono, 13 * s, ImVec2(after + 12 * s, height * 0.165f), uiColor(UiColor::Accent), TextFormat("%d / %d", state.stars, state.starsPossible));
    }

    // Its words, on the left: its pages as a lesson's are drawn (text, a neck, a staff...), its drills left out (they're
    // on the right, with their stars); scrolled with the wheel when they're long
    const float textWidth = width * 0.4f, wordsTop = height * 0.21f, wordsBottom = height * 0.9f;
    const ImVec2 mouseNow = ImGui::GetMousePos();
    if (mouseNow.x < left + textWidth && mouseNow.y > wordsTop && mouseNow.y < wordsBottom)
        wordsScroll -= ImGui::GetIO().MouseWheel * 50.0f * s;
    wordsScroll = std::clamp(wordsScroll, 0.0f, std::max(0.0f, wordsHeight - (wordsBottom - wordsTop)));
    draw->PushClipRect(ImVec2(left - 10 * s, wordsTop - 4 * s), ImVec2(left + textWidth + 10 * s, wordsBottom), true);
    PageState words;
    words.leaveOutScored = true;
    float y = wordsTop - wordsScroll;
    for (int page = 0; page < (int)chapter.doc.pages.size(); page++){
        PageEvents events;
        y += drawLessonPage(chapter.doc, page, "", media, words, events, ImVec2(left, y), textWidth, s) + 20 * s;
    }
    wordsHeight = y + wordsScroll - wordsTop;
    draw->PopClipRect();

    // Its drills, on the right: each a card with its best, what passes it, and a bar
    const float listX = width * 0.52f, rowHeight = 74 * s, top = height * 0.21f, bottom = height * 0.9f;
    const int next = firstNotPassed();
    const bool showNext = hasNext();
    const int rows = (int)drills.size() + (showNext ? 1 : 0);
    const float target = std::clamp(chosen * rowHeight - (bottom - top) * 0.4f, 0.0f, std::max(0.0f, rows * rowHeight + 30 * s - (bottom - top)));
    scroll += (target - scroll) * std::min(1.0f, GetFrameTime() * 10.0f);
    const ImVec2 mouse = ImGui::GetMousePos();
    const bool click = ImGui::IsMouseClicked(ImGuiMouseButton_Left);
    draw->PushClipRect(ImVec2(listX - 10 * s, top - 6 * s), ImVec2(right + 10 * s, bottom), true);
    for (int i = 0; i < (int)drills.size(); i++){
        const CourseDrill& drill = drills[i];
        const float rowTop = top + i * rowHeight - scroll;
        if (rowTop > bottom || rowTop + rowHeight < top) continue;
        auto found = scores.best.find(drill.id);
        const int best = found == scores.best.end() ? -1 : found->second;
        const bool passed = best >= drill.passPercent, perfect = best >= 100, isNext = i == next, isChosen = i == chosen;
        const ImVec2 a(listX, rowTop), b(right, rowTop + rowHeight - 10 * s);
        draw->AddRectFilled(a, b, isChosen ? uiColor(UiColor::Accent, 0.12f) : uiColor(UiColor::Card), 10 * s);
        draw->AddRect(a, b, uiColor(isChosen ? UiColor::Accent : UiColor::StaffLine), 10 * s, 0, (isChosen ? 2.0f : 1.0f) * s);
        draw->AddText(fonts.mono, 12 * s, ImVec2(a.x + 16 * s, a.y + 10 * s), uiColor(isNext ? UiColor::Accent : UiColor::Dim),
                      drill.optional ? "CHALLENGE  ·  OPTIONAL" : isNext ? TextFormat("DRILL %d  ·  NEXT", i + 1) : TextFormat("DRILL %d", i + 1));
        draw->AddText(fonts.bold, 19 * s, ImVec2(a.x + 16 * s, a.y + 26 * s), uiColor(UiColor::Ink), drill.name.c_str());
        // Its bar, its best, and a badge
        const float barX = right - 200 * s, barWidth = 110 * s, barY = a.y + 42 * s;
        draw->AddRectFilled(ImVec2(barX, barY), ImVec2(barX + barWidth, barY + 4 * s), uiColor(UiColor::StaffLine), 2 * s);
        if (best > 0) draw->AddRectFilled(ImVec2(barX, barY), ImVec2(barX + barWidth * best / 100.0f, barY + 4 * s), uiColor(passed ? UiColor::Good : UiColor::Accent), 2 * s);
        draw->AddText(fonts.mono, 12 * s, ImVec2(barX + barWidth + 10 * s, barY - 7 * s), uiColor(UiColor::Dim), best < 0 ? "-" : TextFormat("%d%%", best));
        // Its stars: one passed, two at 95%, three perfect
        const int stars = drillStars(best, drill.passPercent);
        for (int k = 0; k < 3; k++)
            drawStar(draw, ImVec2(right - 66 * s + k * 22 * s, a.y + 18 * s), 9 * s, k < stars ? uiColor(UiColor::Accent) : uiColor(UiColor::StaffLine));
        (void)perfect;
        if (click && mouse.x >= a.x && mouse.x <= b.x && mouse.y >= std::max(a.y, top) && mouse.y <= std::min(b.y, bottom)){
            if (isChosen) startDrill(i);
            chosen = i;
        }
    }
    // Passed: the next chapter, as one more card; said plainly when it starts a new level
    if (showNext){
        const int nextLesson = lesson + 1;
        const CourseLesson& following = course.lessons[nextLesson];
        const bool newLevel = following.unit != chapter.unit;
        const bool isChosen = chosen == (int)drills.size();
        const float rowTop = top + drills.size() * rowHeight + 12 * s - scroll;
        const float cardHeight = newLevel ? 86 * s : 64 * s;
        const ImVec2 a(listX, rowTop), b(right, rowTop + cardHeight);
        draw->AddRectFilled(a, b, uiColor(UiColor::Accent, isChosen ? 0.22f : 0.08f), 10 * s);
        draw->AddRect(a, b, uiColor(UiColor::Accent, isChosen ? 1.0f : 0.5f), 10 * s, 0, (isChosen ? 2.5f : 1.0f) * s);
        const int nextNumber = nextLesson - course.units[following.unit].firstLesson + 1;
        const char* label = newLevel ? TextFormat("NEXT LEVEL  ·  LEVEL %d, CHAPTER %d  ·  N", following.unit + 1, nextNumber)
                                     : TextFormat("NEXT CHAPTER  ·  CHAPTER %d  ·  N", nextNumber);
        draw->AddText(fonts.mono, 12 * s, ImVec2(a.x + 16 * s, a.y + 10 * s), uiColor(UiColor::Accent), label);
        draw->AddText(fonts.bold, 19 * s, ImVec2(a.x + 16 * s, a.y + 26 * s), uiColor(UiColor::Ink), following.lesson.title.c_str());
        if (newLevel)
            draw->AddText(fonts.text, 15 * s, ImVec2(a.x + 16 * s, a.y + 54 * s), uiColor(UiColor::Accent),
                          ("A new level begins: " + course.units[following.unit].title).c_str());
        if (click && mouse.x >= a.x && mouse.x <= b.x && mouse.y >= std::max(a.y, top) && mouse.y <= std::min(b.y, bottom)){
            if (isChosen) load(nextLesson);
            else chosen = (int)drills.size();
        }
    }
    draw->PopClipRect();

    // The chapter passed: said once, big
    const float since = (float)(GetTime() - chapterPassedAt);
    if (since < BANNER_S){
        const char* text = state.perfect ? "Chapter perfect!" : "Chapter passed! The next one is open: Enter.";
        draw->AddText(fonts.heavy, 30 * s, ImVec2(left, bottom - 40 * s), uiColor(UiColor::Good, std::min(1.0f, (BANNER_S - since) * 2.0f)), text);
    }
    menuScreenHint(showNext ? "Up/Down  choose    Enter  play    N  next chapter    Esc  back"
                            : drills.empty() ? "Esc  back" : "Up/Down  choose    Enter  play    Esc  back", s);
    ImGui::Dummy(ImVec2(1, 1)); // the title moved ImGui's cursor: an item after it
}
