#include "learn/chordexercise.h"

#include "audio/audio.h"
#include "core/music.h"
#include "input/noteinput.h"
#include "imgui.h"
#include "raylib.h"
#include "ui/menulist.h"
#include "ui/theme.h"
#include "ui/ui.h"

#include <algorithm>
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
        if (startNoteInput(settings.inputDevice, midiToFrequency(38.0f), inputError)) initStrumDetector(strums, noteInputSampleRate());
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
    }
    pendingStart = -1;
}

void ChordExercise::update(){
    if (ImGui::IsKeyPressed(ImGuiKey_Space)){
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
        lastHeardChord = config.chords[change.chord];
    }
    if (now > passEndTime + 0.3) finishPass();
}

// A chord box, the way chord books draw them: the strings standing up (low E on the left), the nut on top, a dot
// on each fret to press, O over an open string and X over one not played
static void drawChordBox(ImDrawList* draw, const ChordInfo& chord, ImVec2 topLeft, float width, bool current, float s){
    const UiFonts& fonts = uiFonts();
    const int frets = 4;
    int highest = *std::max_element(chord.shape.begin(), chord.shape.end());
    int firstFret = highest > frets ? highest - frets + 1 : 1;
    float stringGap = width / 5, fretGap = stringGap * 1.2f, markTop = topLeft.y, boxTop = topLeft.y + 22 * s;
    ImU32 ink = uiColor(current ? UiColor::Ink : UiColor::Dim);
    for (int string = 0; string < 6; string++){
        verticalLine(draw, topLeft.x + string * stringGap, boxTop, boxTop + frets * fretGap, 1.5f * s, ink);
    }
    for (int fret = 0; fret <= frets; fret++){
        float thickness = (fret == 0 && firstFret == 1 ? 5.0f : 1.5f) * s; // the nut, when the box starts at it
        horizontalLine(draw, topLeft.x, topLeft.x + width, boxTop + fret * fretGap, thickness, ink);
    }
    if (firstFret > 1) draw->AddText(fonts.mono, 13 * s, ImVec2(topLeft.x + width + 8 * s, boxTop + 4 * s), ink, TextFormat("%dfr", firstFret));
    for (int string = 0; string < 6; string++){
        float x = topLeft.x + string * stringGap;
        int fret = chord.shape[string];
        if (fret <= 0){
            const char* mark = fret == 0 ? "O" : "X";
            float markWidth = fonts.mono ? fonts.mono->CalcTextSizeA(14 * s, FLT_MAX, 0.0f, mark).x : 0.0f;
            draw->AddText(fonts.mono, 14 * s, ImVec2(x - markWidth / 2, markTop), ink, mark);
            continue;
        }
        float y = boxTop + (fret - firstFret + 0.5f) * fretGap;
        draw->AddCircleFilled(ImVec2(x, y), stringGap * 0.32f, current ? uiColor(UiColor::Accent) : ink);
    }
}

void ChordExercise::draw(){
    float textTop = ImGui::GetCursorPosY();
    float backWidth = ImGui::CalcTextSize("Back").x + 2 * ImGui::GetStyle().FramePadding.x;
    ImGui::SetCursorPos(ImVec2(ImGui::GetWindowWidth() - backWidth - 20, 20));
    if (ImGui::Button("Back")) leave = true;
    ImGui::SetCursorPosY(textTop);
    menuTitle(title.c_str());

    ImDrawList* draw = ImGui::GetWindowDrawList();
    const UiFonts& fonts = uiFonts();
    float s = menuScale(), width = ImGui::GetWindowWidth();
    float left = width * 0.07f;
    ImVec2 at = ImGui::GetCursorScreenPos();
    int shownTempo = running ? tempo : drillTempo(config.tempo, progress);
    std::string list;
    for (const std::string& name : config.chords) list += (list.empty() ? "" : "  ") + name;
    draw->AddText(fonts.mono, 13 * s, ImVec2(left, at.y), uiColor(UiColor::Dim),
                  TextFormat("%s  ·  %d BPM  ·  BEST CLEAN %d  ·  GOAL %d", list.c_str(), shownTempo, progress.bestCleanTempo,
                             config.tempo.maxTempo));

    // Which chord now, and which next: before the first change, the first one is "next"
    double t = drillTime(), beat = 60.0 / std::max(1, running ? tempo : shownTempo);
    int now = -1;
    if (running) for (int i = 0; i < (int)changes.size(); i++) if (changes[i].time <= t) now = i;
    int currentChord = now >= 0 ? changes[now].chord : -1;
    int nextChord = now + 1 < (int)changes.size() ? changes[now + 1].chord : (running ? -1 : 0);
    if (!running) nextChord = 0;

    float top = at.y + 40 * s, boxWidth = 150 * s;
    auto chordPanel = [&](int chord, float x, bool current, const char* label){
        draw->AddText(fonts.mono, 13 * s, ImVec2(x, top), uiColor(UiColor::Dim), label);
        if (chord < 0) return;
        const ChordInfo& info = *findChord(config.chords[chord]);
        draw->AddText(fonts.heavy, (current ? 64 : 40) * s, ImVec2(x, top + 20 * s), uiColor(current ? UiColor::Ink : UiColor::Dim), info.name.c_str());
        drawChordBox(draw, info, ImVec2(x + 8 * s, top + 110 * s), current ? boxWidth : boxWidth * 0.75f, current, s);
    };
    chordPanel(currentChord, left, true, running ? "NOW" : "");
    chordPanel(nextChord, left + width * 0.36f, false, "NEXT");

    // The beats left before the change: one dot each, lit as they pass
    if (running && now >= 0){
        int beatsIn = (int)((t - changes[now].time) / beat);
        for (int b = 0; b < config.beatsPerChord; b++){
            bool passed = b <= beatsIn;
            draw->AddCircleFilled(ImVec2(left + 8 * s + b * 24 * s, top + 330 * s), 7 * s, uiColor(passed ? UiColor::Accent : UiColor::StaffLine));
        }
    }

    // What was heard, and the last pass
    float textY = top + 360 * s;
    auto line = [&](UiColor color, const std::string& text){
        draw->AddText(fonts.text, 18 * s, ImVec2(left, textY), uiColor(color), text.c_str());
        textY += 26 * s;
    };
    if (!running) line(UiColor::Ink, "Space to start: a bar of clicks, then strum each chord on its first beat");
    else switch (lastHeard){
        case Heard::Right:      line(UiColor::Good, lastHeardChord + ": right, on time"); break;
        case Heard::WrongChord: line(UiColor::Bad, "On time, but that didn't sound like " + lastHeardChord); break;
        case Heard::Missed:     line(UiColor::Bad, lastHeardChord + ": missed"); break;
        default:                line(UiColor::Dim, TextFormat("%d changes so far", hits)); break;
    }
    if (!passText.empty()) line(UiColor::Good, passText);
    if (!inputError.empty()) line(UiColor::Bad, inputError);
    else if (!noteInputActive()) line(UiColor::Dim, "Keyboard: any number key is a strum, and only the timing is checked");
    menuScreenHint("Space  start / stop    Esc  back", s);
    // Everything above is drawn by hand: this tells ImGui how far down the screen's content goes
    ImGui::SetCursorScreenPos(ImVec2(left, textY));
    ImGui::Dummy(ImVec2(0, 0));
}
