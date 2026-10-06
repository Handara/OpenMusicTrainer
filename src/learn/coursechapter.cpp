#include "learn/coursechapter.h"

#include "imgui.h"
#include "raylib.h"
#include "ui/menulist.h"
#include "ui/theme.h"
#include "ui/ui.h"

#include <algorithm>
#include <cfloat>
#include <cmath>

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
    chapterPassedAt = -100.0;
}

bool CourseChapter::hasNext() const {
    return lesson + 1 < (int)course.lessons.size() && chapterState(course, lesson, scores).passed;
}

CourseChapter::~CourseChapter(){
    running.reset();
    ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
}

int CourseChapter::firstNotPassed() const {
    for (int i = 0; i < (int)drills.size(); i++){
        auto found = scores.best.find(drills[i].id);
        if (found == scores.best.end() || found->second < drills[i].passPercent) return i;
    }
    return -1;
}

void CourseChapter::startDrill(int index){
    if (index < 0 || index >= (int)drills.size()) return;
    running.reset();
    runningIndex = index;
    chosen = index;
    passedAt = -1.0;
    lastPercent = -1;
    running = create(entries[index]);
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
    if (!wasPassed && chapterState(course, lesson, scores).passed) chapterPassedAt = GetTime();
}

void CourseChapter::update(){
    if (running){
        running->update();
        int percent = 0;
        if (running->takeFinishedRun(percent)) scored(percent);
        else if (!running->scoresRuns() && passedAt < 0.0){
            // An exercise that doesn't score runs: its lesson goal reached counts as all right
            const LessonStep& step = course.lessons[lesson].lesson.steps[drills[runningIndex].step];
            if (running->lessonScore() >= lessonGoal(step, entries[runningIndex].exercise.type)) scored(100);
        }
        if (running && running->wantsToLeave()) stopDrill();
        else if (passedAt >= 0.0 && GetTime() - passedAt >= NEXT_AFTER_S){
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

    // Its words, on the left
    const float textWidth = width * 0.4f;
    float y = height * 0.21f;
    for (const LessonStep& step : chapter.lesson.steps){
        if (step.type != LessonStepType::Text) continue;
        if (!step.title.empty()){
            draw->AddText(fonts.bold, 22 * s, ImVec2(left, y), uiColor(UiColor::Ink), step.title.c_str(), nullptr, textWidth);
            y += fonts.bold->CalcTextSizeA(22 * s, FLT_MAX, textWidth, step.title.c_str()).y + 8 * s;
        }
        for (const std::string& paragraph : step.paragraphs){
            draw->AddText(fonts.text, 17 * s, ImVec2(left, y), uiColor(UiColor::Ink, 0.85f), paragraph.c_str(), nullptr, textWidth);
            y += fonts.text->CalcTextSizeA(17 * s, FLT_MAX, textWidth, paragraph.c_str()).y + 10 * s;
        }
        y += 8 * s;
    }

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
                      isNext ? TextFormat("DRILL %d  ·  NEXT", i + 1) : TextFormat("DRILL %d", i + 1));
        draw->AddText(fonts.bold, 19 * s, ImVec2(a.x + 16 * s, a.y + 26 * s), uiColor(UiColor::Ink), drill.name.c_str());
        // Its bar, its best, and a badge
        const float barX = right - 200 * s, barWidth = 110 * s, barY = a.y + 42 * s;
        draw->AddRectFilled(ImVec2(barX, barY), ImVec2(barX + barWidth, barY + 4 * s), uiColor(UiColor::StaffLine), 2 * s);
        if (best > 0) draw->AddRectFilled(ImVec2(barX, barY), ImVec2(barX + barWidth * best / 100.0f, barY + 4 * s), uiColor(passed ? UiColor::Good : UiColor::Accent), 2 * s);
        draw->AddText(fonts.mono, 12 * s, ImVec2(barX + barWidth + 10 * s, barY - 7 * s), uiColor(UiColor::Dim), best < 0 ? "-" : TextFormat("%d%%", best));
        if (perfect || passed){
            const char* badge = perfect ? "PERFECT" : "PASSED";
            const ImVec2 size = fonts.mono->CalcTextSizeA(11 * s, FLT_MAX, 0.0f, badge);
            const ImVec2 at(right - size.x - 24 * s, a.y + 10 * s);
            draw->AddRectFilled(ImVec2(at.x - 6 * s, at.y - 3 * s), ImVec2(at.x + size.x + 6 * s, at.y + size.y + 3 * s), uiColor(perfect ? UiColor::Accent : UiColor::Good), 4 * s);
            draw->AddText(fonts.mono, 11 * s, at, uiColor(UiColor::Background), badge);
        }
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
