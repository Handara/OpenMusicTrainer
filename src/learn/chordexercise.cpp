#include "learn/chordexercise.h"

#include "audio/audio.h"
#include "core/music.h"
#include "input/noteinput.h"
#include "imgui.h"
#include "raylib.h"
#include "ui/fretboardview.h"
#include "ui/neckcards.h"
#include "ui/menulist.h"
#include "ui/scoreboard.h"
#include "ui/theme.h"
#include "ui/ui.h"

#include <algorithm>
#include <cfloat>
#include <cmath>

const double LEAD_IN_S = 0.3;
const double LOOKAHEAD_S = 0.2;
const double STRUM_WINDOW_S = 0.15;  // a strum this close to the change counts: a strum takes longer than a pick
const double LISTEN_FROM_S = 0.03;   // the chord is checked from just after the strum's attack...
const double LISTEN_FOR_S = 0.17;    // ...for this long
const double HISTORY_S = 1.0;
const int STANDARD_TUNING[6] = { 40, 45, 50, 55, 59, 64 };

ChordExercise::ChordExercise(const std::string& title, const ChordDrillConfig& config, const std::string& progressPath,
                             const Settings& settings)
    : title(title), config(config), progressPath(progressPath), settings(settings){
    progress = loadDrillProgress(progressPath);
    if (settings.playWithInstrument){
        if (startNoteInput(settings.inputDevice, midiToFrequency(38.0f), inputError, settings.guitarChannel)) initStrumDetector(strums, noteInputSampleRate());
        else inputError = "Instrument: " + inputError + " (using the keyboard: only the timing is checked)";
    }
    ImGui::GetIO().ConfigFlags &= ~ImGuiConfigFlags_NavEnableKeyboard; // Space and the number keys play here
}

ChordExercise::~ChordExercise(){
    stopPreviews();
    stopNoteInput();
    ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
}

double ChordExercise::drillTime() const {
    return audioTime() - settings.globalOffsetMs / 1000.0;
}

// A bar of count-in, then every chord of the list for its beats, the list `rounds` times
void ChordExercise::startPass(){
    tempo = drillTempo(config.tempo, progress);
    double beat = 60.0 / tempo;
    countInStart = audioTime() + LEAD_IN_S;
    double first = countInStart + config.beatsPerChord * beat;
    changes.clear();
    int count = (int)config.chords.size() * config.rounds;
    for (int i = 0; i < count; i++) changes.push_back({first + i * config.beatsPerChord * beat, i % (int)config.chords.size()});
    passEndTime = first + count * config.beatsPerChord * beat;
    nextClick = 0;
    totalClicks = config.beatsPerChord * (count + 1);
    hits = 0;
    pendingStart = -1;
    lastHeard = Heard::Nothing;
}

void ChordExercise::finishPass(){
    int total = (int)changes.size();
    float accuracy = total > 0 ? 100.0f * hits / total : 0.0f;
    DrillPassOutcome outcome = finishDrillPass(config.tempo, progress, tempo, accuracy);
    if (outcome.clean) cleanPassesNow++;
    std::string error;
    saveDrillProgress(progressPath, progress, error);
    passText = TextFormat("Last pass at %d bpm: %d of %d changes (%.0f%%). ", tempo, hits, total, accuracy);
    if (outcome.newBest) passText += TextFormat("New best clean tempo! Next: %d bpm", outcome.nextTempo);
    else if (outcome.clean) passText += TextFormat("Clean. Next: %d bpm", outcome.nextTempo);
    else if (outcome.nextTempo < tempo) passText += TextFormat("Slowing down to %d bpm", outcome.nextTempo);
    else passText += TextFormat("Again at %d bpm (%d%% to speed up)", outcome.nextTempo, config.tempo.passPercent);
    startPass();
}

// A strum at `time`: on time for a change, it's that change's. From the instrument, the chord is checked once
// enough of its sound has come in (checkPendingChord); from the keyboard, the timing is all there is.
void ChordExercise::strummed(double time, long long sample){
    int nearest = -1;
    for (int i = 0; i < (int)changes.size(); i++){
        if (changes[i].judged || std::abs(changes[i].time - time) > STRUM_WINDOW_S) continue;
        if (nearest < 0 || std::abs(changes[i].time - time) < std::abs(changes[nearest].time - time)) nearest = i;
    }
    if (nearest < 0) return; // a stray strum between changes
    if (sample < 0){
        changes[nearest].judged = changes[nearest].hit = true;
        hits++;
        lastHeard = Heard::Right;
        lastHeardAt = GetTime();
        lastHeardChord = config.chords[changes[nearest].chord];
        return;
    }
    pendingStart = sample + (long long)(LISTEN_FROM_S * noteInputSampleRate());
    pendingChange = nearest;
}

void ChordExercise::checkPendingChord(){
    if (pendingStart < 0) return;
    int rate = noteInputSampleRate();
    long long length = (long long)(LISTEN_FOR_S * rate);
    if (historyEnd < pendingStart + length) return; // not all of it has come in yet
    long long historyStart = historyEnd - (long long)history.size();
    Change& change = changes[pendingChange];
    const ChordInfo& chord = *findChord(config.chords[change.chord]);
    lastHeardChord = chord.name;
    if (pendingStart >= historyStart){
        std::array<float, 12> notes = chroma(history.data() + (pendingStart - historyStart), (int)length, rate);
        change.judged = true;
        change.hit = soundsLikeChord(notes, chord);
        if (change.hit) hits++;
        lastHeard = change.hit ? Heard::Right : Heard::WrongChord;
        lastHeardAt = GetTime();
    }
    pendingStart = -1;
}

void ChordExercise::update(){
    if (ImGui::IsKeyPressed(ImGuiKey_Space, false)){ // not repeated: held, it would stop what it started
        running = !running;
        if (running) startPass();
        else stopPreviews();
    }
    // The input keeps flowing while paused (so it's never stale), and its samples are kept a second back
    if (noteInputActive()){
        updateNoteInput();
        const std::vector<float>& samples = latestInputSamples();
        std::vector<long long> found;
        feedStrumDetector(strums, samples.data(), (int)samples.size(), found);
        history.insert(history.end(), samples.begin(), samples.end());
        historyEnd += (long long)samples.size();
        size_t keep = (size_t)(HISTORY_S * std::max(1, noteInputSampleRate()));
        if (history.size() > keep) history.erase(history.begin(), history.end() - keep);
        if (running){
            for (long long start : found){
                double age = (double)(historyEnd - start) / noteInputSampleRate();
                strummed(drillTime() - age - settings.inputOffsetMs / 1000.0, start);
            }
        }
    }
    if (!running) return;

    double now = audioTime(), beat = 60.0 / tempo;
    while (nextClick < totalClicks && countInStart + nextClick * beat < now + LOOKAHEAD_S){
        playClickAt(countInStart + nextClick * beat, nextClick % config.beatsPerChord == 0); // accented on the change
        nextClick++;
    }
    double t = drillTime();
    for (int key = 0; key < 6; key++) if (IsKeyPressed(KEY_ONE + key)) strummed(t, -1);
    checkPendingChord();
    // A change nobody strummed, once it's too late to: missed
    for (Change& change : changes){
        if (change.judged || t <= change.time + STRUM_WINDOW_S || (pendingStart >= 0 && &change == &changes[pendingChange])) continue;
        change.judged = true;
        lastHeard = Heard::Missed;
        lastHeardAt = GetTime();
        lastHeardChord = config.chords[change.chord];
    }
    if (now > passEndTime + 0.3) finishPass();
}

// The chord exercise, on play mode's neck: the chord now as cards on its strings (an X on a string left out), the next
// one outlined where the fingers go; above it, the chords as a lane of blocks, as long as their beats, a playhead going
// along; and what was heard of the last change, big
void ChordExercise::draw(){
    menuTitle(title.c_str());
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const UiFonts& fonts = uiFonts();
    const float s = menuScale(), width = ImGui::GetWindowWidth(), height = ImGui::GetWindowHeight();
    const float left = width * 0.07f, right = width * 0.93f;
    const int shownTempo = running ? tempo : drillTempo(config.tempo, progress);
    drawScoreboard({
        { "TEMPO", TextFormat("%d", shownTempo), UiColor::Ink, "bpm" },
        { "CHANGES", running ? std::string(TextFormat("%d", hits)) : std::string("-"), UiColor::Good, running ? TextFormat("of %d", (int)changes.size()) : "" },
        { "BEST CLEAN", progress.bestCleanTempo > 0 ? std::string(TextFormat("%d", progress.bestCleanTempo)) : std::string("-"), UiColor::Accent, "bpm" },
        { "GOAL", TextFormat("%d", config.tempo.maxTempo), progress.bestCleanTempo >= config.tempo.maxTempo ? UiColor::Good : UiColor::Ink, "bpm" },
    }, right, height * 0.03f + 36 * s, s);

    // Which chord now, and which next: before the first change, the first one is next
    const double t = drillTime(), beat = 60.0 / std::max(1, shownTempo);
    int now = -1;
    if (running) for (int i = 0; i < (int)changes.size(); i++) if (changes[i].time <= t) now = i;
    const int currentChord = now >= 0 ? changes[now].chord : -1;
    const int nextChord = !running ? 0 : now + 1 < (int)changes.size() ? changes[now + 1].chord : -1;

    // The lane: the chords as blocks in a row, each as long as its beats, a playhead going along (the count-in first)
    const float laneTop = height * 0.22f, laneHeight = 46 * s, laneWidth = right - left;
    const int count = running ? (int)changes.size() : (int)config.chords.size();
    const double laneStart = running ? countInStart + config.beatsPerChord * beat : 0.0;
    const float blockWidth = std::max(70 * s, laneWidth / std::max(4, std::min(count, 8)));
    // The lane scrolls to keep the playhead a third of the way in
    const double into = running ? (t - laneStart) / (config.beatsPerChord * beat) : 0.0;
    const float scroll = std::max(0.0f, (float)into * blockWidth - laneWidth / 3);
    draw->PushClipRect(ImVec2(left, laneTop - 4 * s), ImVec2(right, laneTop + laneHeight + 22 * s), true);
    for (int i = 0; i < count; i++){
        const int chord = running ? changes[i].chord : i;
        const float x = left + i * blockWidth - scroll;
        if (x > right || x + blockWidth < left) continue;
        const bool isNow = i == now, done = running && i < now;
        const ImU32 fill = isNow ? uiColor(UiColor::Accent, 0.22f) : uiColor(UiColor::Card);
        ImU32 edge = uiColor(UiColor::StaffLine);
        if (done) edge = changes[i].hit ? uiColor(UiColor::Good, 0.8f) : uiColor(UiColor::Bad, 0.8f);
        if (isNow) edge = uiColor(UiColor::Accent);
        draw->AddRectFilled(ImVec2(x + 2 * s, laneTop), ImVec2(x + blockWidth - 2 * s, laneTop + laneHeight), fill, 8 * s);
        draw->AddRect(ImVec2(x + 2 * s, laneTop), ImVec2(x + blockWidth - 2 * s, laneTop + laneHeight), edge, 8 * s, 0, (isNow ? 2.5f : 1.5f) * s);
        const char* name = config.chords[chord].c_str();
        draw->AddText(fonts.bold, 22 * s, ImVec2(x + 14 * s, laneTop + laneHeight / 2 - 12 * s), uiColor(isNow ? UiColor::Ink : done ? UiColor::Dim : UiColor::Ink), name);
        // Its beats, ticks along its bottom; the first is the strum
        for (int b = 0; b < config.beatsPerChord; b++){
            const float bx = x + 2 * s + (blockWidth - 4 * s) * b / config.beatsPerChord;
            draw->AddLine(ImVec2(bx, laneTop + laneHeight - (b == 0 ? 14 : 7) * s), ImVec2(bx, laneTop + laneHeight), uiColor(b == 0 ? UiColor::Accent : UiColor::Dim, 0.8f), 2 * s);
        }
    }
    if (running){
        const float x = left + (float)into * blockWidth - scroll;
        draw->AddLine(ImVec2(x, laneTop - 4 * s), ImVec2(x, laneTop + laneHeight + 4 * s), uiColor(UiColor::Ink), 3 * s);
        // Counting in: the beats left before the first change, big
        if (into < 0.0){
            const int left4 = (int)std::ceil(-into * config.beatsPerChord);
            const char* text = TextFormat("%d", left4);
            draw->AddText(fonts.heavy, 30 * s, ImVec2(left + 8 * s, laneTop + laneHeight + 2 * s), uiColor(UiColor::Accent), text);
        }
    }
    draw->PopClipRect();

    // The neck: the chord now as cards, the next outlined where the fingers go
    const std::vector<int> tuning(std::begin(STANDARD_TUNING), std::end(STANDARD_TUNING));
    const float spacing = std::min(42.0f, height * 0.34f / s / 6.0f);
    FretboardLayout board = fretboardLayout(left, height * 0.41f, right - left, s, 6, 0, 12, spacing);
    drawFretboard(board, tuning);
    float halfW, halfH;
    cardSize(board, halfW, halfH);
    const double sinceHeard = GetTime() - lastHeardAt;
    const bool flash = sinceHeard < 0.5;
    const int shown = currentChord >= 0 ? currentChord : nextChord;
    if (nextChord >= 0 && nextChord != shown){
        const ChordInfo* next = findChord(config.chords[nextChord]);
        for (int string = 0; next && string < 6; string++){
            if (next->shape[string] < 0) continue;
            cardOutline(draw, ImVec2(board.fretX(next->shape[string]), board.stringY(string)), halfW, halfH, 0.0f, uiColor(UiColor::Accent, 0.55f), 2 * s);
        }
    }
    if (shown >= 0){
        const ChordInfo* chord = findChord(config.chords[shown]);
        const float pop = now >= 0 ? 4.0f * s * std::exp(-(float)(t - changes[now].time) * 10.0f) : 0.0f;
        for (int string = 0; chord && string < 6; string++){
            const float y = board.stringY(string);
            if (chord->shape[string] < 0){
                const ImVec2 size = fonts.bold->CalcTextSizeA(18 * s, FLT_MAX, 0.0f, "X");
                draw->AddText(fonts.bold, 18 * s, ImVec2(board.fretX(0) - size.x / 2, y - size.y / 2), uiColor(UiColor::Bad, 0.85f), "X"); // not played
                continue;
            }
            const int fret = chord->shape[string];
            drawNoteCard(draw, board, string, fret, tuning[string] + fret, pop, currentChord >= 0 ? 1.0f : 0.6f, currentChord >= 0, s);
            if (flash && (lastHeard == Heard::Right || lastHeard == Heard::WrongChord || lastHeard == Heard::Missed))
                cardOutline(draw, ImVec2(board.fretX(fret), y), halfW, halfH, 3 * s + 8 * s * (float)sinceHeard,
                            uiColor(lastHeard == Heard::Right ? UiColor::Good : UiColor::Bad, 1.0f - (float)sinceHeard * 2.0f), 2.5f * s);
        }
        // The chord's name by the neck, big
        draw->AddText(fonts.heavy, 46 * s, ImVec2(left, board.top - 62 * s), uiColor(currentChord >= 0 ? UiColor::Ink : UiColor::Dim), config.chords[shown].c_str());
        if (nextChord >= 0 && nextChord != shown)
            draw->AddText(fonts.bold, 18 * s, ImVec2(left + 140 * s, board.top - 44 * s), uiColor(UiColor::Accent), ("next " + config.chords[nextChord]).c_str());
    }

    // What was heard at the last change, big, then what to do
    float textY = board.top + board.height + 34 * s;
    if (running && lastHeard != Heard::Nothing && sinceHeard < 1.5){
        const char* verdict = lastHeard == Heard::Right ? "RIGHT" : lastHeard == Heard::WrongChord ? "WRONG CHORD" : "MISSED";
        const UiColor color = lastHeard == Heard::Right ? UiColor::Good : UiColor::Bad;
        draw->AddText(fonts.heavy, 30 * s, ImVec2(left, textY), uiColor(color, (float)std::min(1.0, 3.0 - 2.0 * sinceHeard)), verdict);
        if (lastHeard == Heard::WrongChord)
            draw->AddText(fonts.text, 16 * s, ImVec2(left + 220 * s, textY + 10 * s), uiColor(UiColor::Dim), ("on time, but it didn't sound like " + lastHeardChord).c_str());
        textY += 44 * s;
    } else if (!running){
        draw->AddText(fonts.bold, 20 * s, ImVec2(left, textY), uiColor(UiColor::Ink), "Space to start.");
        draw->AddText(fonts.text, 16 * s, ImVec2(left, textY + 28 * s), uiColor(UiColor::Dim),
                      "A bar of clicks counts you in. Then strum each chord as the playhead reaches its block. The next chord is outlined on the neck.");
        textY += 56 * s;
    }
    if (!passText.empty()){
        draw->AddText(fonts.text, 16 * s, ImVec2(left, textY), uiColor(UiColor::Good), passText.c_str());
        textY += 24 * s;
    }
    if (!inputError.empty()) draw->AddText(fonts.text, 16 * s, ImVec2(left, textY), uiColor(UiColor::Bad), inputError.c_str());
    else if (!noteInputActive()) draw->AddText(fonts.text, 16 * s, ImVec2(left, textY), uiColor(UiColor::Dim), "Keyboard: any number key is a strum, and only the timing is checked");
    menuScreenHint("Space  start / stop    Esc  back", s);
    ImGui::Dummy(ImVec2(1, 1)); // the board moved ImGui's cursor (ui/fretboardview): an item after it
}
