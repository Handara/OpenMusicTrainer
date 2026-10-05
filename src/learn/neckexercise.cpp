#include "learn/neckexercise.h"

#include "audio/audio.h"
#include "core/music.h"
#include "core/routine.h"
#include "imgui.h"
#include "input/noteinput.h"
#include "raylib.h"
#include "ui/fretboardview.h"
#include "ui/menulist.h"
#include "ui/neckcards.h"
#include "ui/scoreboard.h"
#include "ui/theme.h"
#include "ui/ui.h"
#include "views/playnote.h"

#include <algorithm>
#include <cfloat>
#include <cmath>

const std::vector<int> NECK_GUITAR = { 40, 45, 50, 55, 59, 64 };
const std::vector<int> NECK_BASS = { 28, 33, 38, 43 };
const int NECK_GUITAR_FRETS = 22, NECK_BASS_FRETS = 20;
const float NECK_TRAVEL_S = 0.16f;  // the light going to the next note once one is played
const float WRONG_FLASH_S = 0.6f;   // a wrong note shows this long
const int SHOWN_AHEAD = 2;          // notes shown after the next, fainter
const int HISTORY_BARS = 20;        // past runs drawn after a run
// The scales offered, in the order the button goes round them
const char* const NECK_SCALES[] = { "major", "minor", "major_pentatonic", "minor_pentatonic", "blues", "dorian", "mixolydian",
                                    "harmonic_minor" };

static std::string clockText(double seconds){
    return TextFormat("%d:%04.1f", (int)seconds / 60, std::fmod(seconds, 60.0));
}

static std::string todayText(){
    int year, month, day;
    dateFromDays(today(), year, month, day);
    return TextFormat("%04d-%02d-%02d", year, month, day);
}

NeckExercise::NeckExercise(const std::string& title, const NeckRoutine& routine, bool onBass, const std::string& progressPath,
                           const Settings& settings)
    : title(title), routine(routine), onBass(onBass), tuning(onBass ? NECK_BASS : NECK_GUITAR),
      frets(onBass ? NECK_BASS_FRETS : NECK_GUITAR_FRETS), progressPath(progressPath), settings(settings){
    stats = loadNeckStats(progressPath);
    const InputRole role = onBass ? InputRole::Bass : InputRole::Guitar;
    listening = startNoteInput(settings.inputDevice, midiToFrequency((float)tuning.front()) * 0.9f, inputError, channelFor(settings, role));
    ImGui::GetIO().ConfigFlags &= ~ImGuiConfigFlags_NavEnableKeyboard; // the keys change the routine here
    restart();
}

NeckExercise::~NeckExercise(){
    stopNoteInput();
    ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
}

void NeckExercise::restart(){
    stepsError.clear();
    if (!neckSteps(routine, tuning, frets, steps, stepsError)) steps.clear();
    startNeckRun(run, steps);
    finished = false;
    lastRightAt = wrongAt = -100.0;
}

void NeckExercise::played(int pitch){
    if (finished || steps.empty()) return;
    const double now = GetTime();
    if (playNeckNote(run, pitch, now)){
        lastRightAt = now;
        if (neckRunDone(run)) finish();
    } else {
        wrongAt = now;
    }
}

void NeckExercise::finish(){
    finished = true;
    finishedAt = GetTime();
    if (run.mistakes == 0) cleanRuns++;
    addNeckRun(stats, run, neckRoutineName(routine), todayText());
    std::string error;
    if (!saveNeckStats(progressPath, stats, error)) TraceLog(LOG_WARNING, "Progress: %s", error.c_str());
}

void NeckExercise::update(){
    if (listening) for (const PlayedNote& note : updateNoteInput()) played(note.pitch);
    // The routine, changed from the keyboard (the buttons do the same): each change starts it again
    const bool shift = ImGui::GetIO().KeyShift;
    bool changed = true;
    if (ImGui::IsKeyPressed(ImGuiKey_K)) routine.rootPitchClass = (routine.rootPitchClass + (shift ? 11 : 1)) % 12;
    else if (ImGui::IsKeyPressed(ImGuiKey_N)) routine.notePitchClass = (routine.notePitchClass + (shift ? 11 : 1)) % 12;
    else if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) routine.position = std::min(frets - 3, routine.position + 1);
    else if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) routine.position = std::max(0, routine.position - 1);
    else if (ImGui::IsKeyPressed(ImGuiKey_Tab)) routine.pattern = (NeckPattern)(((int)routine.pattern + (shift ? 3 : 1)) % 4);
    else if (ImGui::IsKeyPressed(ImGuiKey_F)) routine.fingering = routine.fingering == Fingering::Position ? Fingering::ThreeNotesPerString : Fingering::Position;
    else if (ImGui::IsKeyPressed(ImGuiKey_S)){
        const int count = (int)(sizeof NECK_SCALES / sizeof NECK_SCALES[0]);
        int at = 0;
        while (at < count && routine.scale != NECK_SCALES[at]) at++;
        routine.scale = NECK_SCALES[(at + (shift ? count - 1 : 1)) % count];
    }
    else if (ImGui::IsKeyPressed(ImGuiKey_Space) || ImGui::IsKeyPressed(ImGuiKey_Enter)) changed = true;
    else changed = false;
    if (changed) restart();
    if (ImGui::IsKeyPressed(ImGuiKey_Escape)) leave = true;
}

// The routine, as buttons: each click goes to the next choice (its key does the same)
void NeckExercise::drawControls(float left, float top, float s){
    const ScaleInfo* scale = findScale(routine.scale);
    struct Chip { std::string text; const char* key; };
    std::vector<Chip> chips;
    if (routine.pattern == NeckPattern::EveryString){
        chips.push_back({ std::string("Note ") + pitchClassName(routine.notePitchClass), "N" });
        chips.push_back({ "From fret " + std::to_string(routine.position), "Up Down" });
    } else {
        chips.push_back({ std::string("Key ") + pitchClassName(routine.rootPitchClass), "K" });
        chips.push_back({ scale ? scale->displayName : routine.scale, "S" });
        chips.push_back({ routine.fingering == Fingering::Position ? "In a position" : "3 per string", "F" });
        chips.push_back({ (routine.fingering == Fingering::Position ? "Position " : "From fret ") + std::to_string(routine.position), "Up Down" });
    }
    chips.push_back({ neckPatternName(routine.pattern), "Tab" });
    const UiFonts& fonts = uiFonts();
    float x = left;
    for (size_t i = 0; i < chips.size(); i++){
        if (menuPill(chips[i].text.c_str(), chips[i].key, ImVec2(x, top), false, 0, s)){
            // A click: as its key
            const bool every = routine.pattern == NeckPattern::EveryString;
            const size_t last = chips.size() - 1;
            if (i == last) routine.pattern = (NeckPattern)(((int)routine.pattern + 1) % 4);
            else if (every && i == 0) routine.notePitchClass = (routine.notePitchClass + 1) % 12;
            else if ((every && i == 1) || (!every && i == 3)) routine.position = routine.position + 1 > frets - 3 ? 0 : routine.position + 1;
            else if (i == 0) routine.rootPitchClass = (routine.rootPitchClass + 1) % 12;
            else if (i == 1){
                const int count = (int)(sizeof NECK_SCALES / sizeof NECK_SCALES[0]);
                int at = 0;
                while (at < count && routine.scale != NECK_SCALES[at]) at++;
                routine.scale = NECK_SCALES[(at + 1) % count];
            }
            else if (i == 2) routine.fingering = routine.fingering == Fingering::Position ? Fingering::ThreeNotesPerString : Fingering::Position;
            restart();
        }
        const float textWidth = fonts.bold->CalcTextSizeA(16 * s, FLT_MAX, 0.0f, chips[i].text.c_str()).x;
        const float keyWidth = fonts.mono->CalcTextSizeA(12 * s, FLT_MAX, 0.0f, chips[i].key).x;
        x += 12 * s + textWidth + 12 * s + keyWidth + 12 * s + 10 * s;
    }
}

// The scoreboard (ui/scoreboard): the notes, the mistakes, the time and the best, at the top
void NeckExercise::drawScoreboard(float right, float top, float s){
    const double seconds = run.startedAt < 0.0 ? 0.0 : (finished ? neckRunSeconds(run) : GetTime() - run.startedAt);
    const float best = neckBestSeconds(stats, neckRoutineName(routine));
    ::drawScoreboard({
        { "NOTES", std::to_string(run.next), UiColor::Ink, TextFormat("of %d", (int)steps.size()) },
        { "MISTAKES", std::to_string(run.mistakes), run.mistakes == 0 ? UiColor::Good : UiColor::Bad, "" },
        { "TIME", clockText(seconds), UiColor::Ink, "" },
        { "BEST", best < 0.0f ? std::string("-") : clockText(best), UiColor::Accent, TextFormat("%d clean this time", cleanRuns) },
    }, right, top, s);
}

// After a run: how it went, against the best, and the runs before it as bars (shorter is quicker)
void NeckExercise::drawResults(float left, float top, float width, float s){
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const UiFonts& fonts = uiFonts();
    const double seconds = neckRunSeconds(run);
    const float best = neckBestSeconds(stats, neckRoutineName(routine));
    const int perMinute = seconds > 0.0 ? (int)std::lround(steps.size() / seconds * 60.0) : 0;
    const bool newBest = best >= 0.0f && std::fabs((float)seconds - best) < 0.001f && run.mistakes == 0;
    std::string line = TextFormat("Done in %s  ·  %d %s  ·  %d notes a minute", clockText(seconds).c_str(), run.mistakes,
                                  run.mistakes == 1 ? "mistake" : "mistakes", perMinute);
    draw->AddText(fonts.bold, 22 * s, ImVec2(left, top), uiColor(run.mistakes == 0 ? UiColor::Good : UiColor::Ink), line.c_str());
    draw->AddText(fonts.mono, 13 * s, ImVec2(left, top + 30 * s), uiColor(newBest ? UiColor::Accent : UiColor::Dim),
                  newBest ? "A NEW BEST  ·  SPACE  AGAIN" : "SPACE  AGAIN    ·    CHANGE ANYTHING ABOVE TO GO ON");
    // The routine's runs, the latest on the right
    std::vector<NeckRecord> runs = neckRunsOf(stats, neckRoutineName(routine));
    if (runs.size() > (size_t)HISTORY_BARS) runs.erase(runs.begin(), runs.end() - HISTORY_BARS);
    float longest = 0.0f;
    for (const NeckRecord& past : runs) longest = std::max(longest, past.seconds);
    const float barWidth = 10 * s, gap = 4 * s, height = 46 * s, x0 = left + width - runs.size() * (barWidth + gap);
    for (size_t i = 0; i < runs.size(); i++){
        const float h = longest > 0.0f ? height * runs[i].seconds / longest : 0.0f;
        const float x = x0 + i * (barWidth + gap);
        const UiColor color = i + 1 == runs.size() ? UiColor::Accent : runs[i].mistakes == 0 ? UiColor::Good : UiColor::Dim;
        draw->AddRectFilled(ImVec2(x, top + height - h), ImVec2(x + barWidth, top + height), uiColor(color, 0.85f), 2 * s);
    }
    const char* label = "YOUR RUNS, TIME";
    const float labelWidth = fonts.mono->CalcTextSizeA(11 * s, FLT_MAX, 0.0f, label).x;
    if (!runs.empty()) draw->AddText(fonts.mono, 11 * s, ImVec2(left + width - labelWidth, top + height + 6 * s), uiColor(UiColor::Dim), label);
}

void NeckExercise::draw(){
    menuTitle(title.c_str());
    const float s = menuScale(), width = ImGui::GetWindowWidth(), height = ImGui::GetWindowHeight();
    const float left = width * 0.07f, right = width * 0.93f;
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const UiFonts& fonts = uiFonts();
    const double now = GetTime();

    drawScoreboard(right, height * 0.03f + 36 * s, s);
    drawControls(left, height * 0.2f, s);
    draw->AddText(fonts.text, 16 * s, ImVec2(left, height * 0.2f + 40 * s), uiColor(UiColor::Dim), neckRoutineName(routine).c_str());

    // The neck, as play mode draws its notes
    const float spacing = std::min(48.0f, height * 0.4f / s / (float)tuning.size());
    FretboardLayout board = fretboardLayout(left, height * 0.33f, right - left, s, (int)tuning.size(), 0, frets, spacing);
    drawFretboard(board, tuning);
    if (!stepsError.empty()){
        draw->AddText(fonts.bold, 18 * s, ImVec2(left, board.top + board.height + 30 * s), uiColor(UiColor::Bad),
                      ("This doesn't fit here: " + stepsError + ". Change the position or the shape.").c_str());
        return;
    }
    float halfW, halfH;
    cardSize(board, halfW, halfH);
    auto at = [&](const NeckStep& step){ return ImVec2(board.fretX(step.fret), board.stringY(step.string)); };
    // The shape: every place the routine goes, outlined; before a run and after it, filled by how quick it is to find
    // (green quick, red slow: the average time to it, every routine together)
    const bool showMap = run.next == 0 || finished;
    std::vector<std::pair<int, int>> places;
    for (const NeckStep& step : steps){
        if (std::find(places.begin(), places.end(), std::make_pair(step.string, step.fret)) != places.end()) continue;
        places.push_back({ step.string, step.fret });
        const ImVec2 p = at(step);
        auto cell = stats.cells.find({ step.string, step.fret });
        if (showMap && cell != stats.cells.end() && cell->second.second > 0){
            const float average = cell->second.first / cell->second.second;
            const float slow = std::clamp((average - 0.4f) / 1.2f, 0.0f, 1.0f); // 0.4 s quick, 1.6 s slow
            const Color quick = themeColor(UiColor::Good), slowColor = themeColor(UiColor::Bad);
            draw->AddRectFilled(ImVec2(p.x - halfW, p.y - halfH), ImVec2(p.x + halfW, p.y + halfH), imColor(blend(quick, slowColor, slow), 0.35f), 0.4f * halfH);
        }
        cardOutline(draw, p, halfW, halfH, 0.0f, uiColor(UiColor::Dim, 0.5f), 1.5f * s);
    }
    if (finished){
        drawResults(left, board.top + board.height + 34 * s, right - left, s);
    } else {
        // The way to the next note, and the next ones: the next lit, the few after fainter
        const size_t next = run.next;
        if (next > 0 && next < steps.size()){
            const float since = (float)(now - lastRightAt);
            drawWay(draw, board, at(steps[next - 1]), at(steps[next]), since < NECK_TRAVEL_S ? since / NECK_TRAVEL_S : -1.0f, 0.9f, s);
            drawNoteCard(draw, board, steps[next - 1].string, steps[next - 1].fret, steps[next - 1].pitch, 0.0f, 0.45f, false, s);
        }
        for (int ahead = SHOWN_AHEAD; ahead >= 0; ahead--){
            const size_t i = next + ahead;
            if (i >= steps.size()) continue;
            const float pop = ahead == 0 ? 4.0f * s * std::exp(-(float)(now - lastRightAt) * 12.0f) : 0.0f;
            drawNoteCard(draw, board, steps[i].string, steps[i].fret, steps[i].pitch, pop, ahead == 0 ? 1.0f : 0.5f - 0.15f * ahead, ahead == 0, s);
        }
        // A wrong note: a red ring round the one to play, and what was heard
        const float wrong = (float)(now - wrongAt);
        if (wrong < WRONG_FLASH_S && next < steps.size()){
            cardOutline(draw, at(steps[next]), halfW, halfH, 4 * s + 10 * s * wrong / WRONG_FLASH_S, uiColor(UiColor::Bad, 1.0f - wrong / WRONG_FLASH_S), 2.5f * s);
            draw->AddText(fonts.bold, 18 * s, ImVec2(left, board.top + board.height + 34 * s), uiColor(UiColor::Bad),
                          TextFormat("You played %s%d", pitchClassName(run.lastWrongPitch), pitchOctave(run.lastWrongPitch)));
        } else if (next == 0){
            draw->AddText(fonts.text, 16 * s, ImVec2(left, board.top + board.height + 34 * s), uiColor(UiColor::Dim),
                          listening ? "Play the lit note to start: the clock starts with it." : "Click the lit note to start (no instrument found: clicking plays it).");
        }
    }
    // A click on a fret plays its note: for trying without an instrument
    const ImVec2 mouse = ImGui::GetMousePos();
    const int string = board.stringAt(mouse.y), fret = board.fretAt(mouse.x);
    if (string >= 0 && fret >= 0 && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) played(tuning[string] + fret);

    menuScreenHint("Play the lit note    K key  S scale  F shape  Up/Down position  Tab pattern  N note    Space  again    Esc  back", s);
    ImGui::Dummy(ImVec2(1, 1)); // the board moved ImGui's cursor (ui/fretboardview): an item after it, as ImGui wants
}
