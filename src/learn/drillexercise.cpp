#include "learn/drillexercise.h"

#include "audio/audio.h"
#include "core/music.h"
#include "imgui.h"
#include "input/noteinput.h"
#include "raylib.h"
#include "ui/ui.h"
#include "views/noteviews.h"

#include <algorithm>
#include <cmath>

const int COUNT_IN_BEATS = 4;      // one bar of clicks before the notes
const double LEAD_IN_S = 0.3;      // silence before the count-in
const double LOOKAHEAD_S = 0.2;    // metronome clicks are handed to the audio engine this far ahead
const float HIT_LINE_X = 180.0f;
const int MAX_KEY_LANES = 6;
const ImU32 TEXT_DIM = IM_COL32(220, 200, 180, 200);
const ImU32 TEXT_GOOD = IM_COL32(120, 220, 130, 255);

DrillExercise::DrillExercise(const std::string& title, const ScaleDrillConfig& config, const std::string& progressPath,
                             const Settings& settings)
    : title(title), config(config), progressPath(progressPath), settings(settings){
    progress = loadDrillProgress(progressPath);
    std::string error;
    buildScaleDrill(config, drillNotes, error); // the file was checked when it loaded, so this succeeds
    chart = drillChart(config, drillNotes);
    if (settings.playWithInstrument){
        float lowest = midiToFrequency((float)*std::min_element(config.tuning.begin(), config.tuning.end())) * 0.9f;
        if (!startNoteInput(settings.inputDevice, lowest, inputError)) inputError = "Instrument: " + inputError + " (using the keyboard)";
    }
    ImGui::GetIO().ConfigFlags &= ~ImGuiConfigFlags_NavEnableKeyboard; // Space and the number keys play here
}

DrillExercise::~DrillExercise(){
    stopPreviews(); // clicks already handed to the audio engine would otherwise still play after leaving
    stopNoteInput();
    ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
}

double DrillExercise::drillTime() const {
    return audioTime() - settings.globalOffsetMs / 1000.0;
}

// Lays one pass out in audio time: a bar of count-in clicks, then the notes, clicked on every beat
void DrillExercise::startPass(){
    tempo = drillTempo(config, progress);
    double beat = 60.0 / tempo;
    countInStart = audioTime() + LEAD_IN_S;
    firstNoteTime = countInStart + COUNT_IN_BEATS * beat;
    double lastBeat = drillNotes.empty() ? 0.0 : drillNotes.back().beat;
    passEndTime = firstNoteTime + lastBeat * beat + NEAR_WINDOW_S + 0.2;
    nextClick = 0;
    totalClicks = COUNT_IN_BEATS + (int)std::ceil(lastBeat) + 1;

    notes.clear();
    for (const DrillNote& note : drillNotes){
        notes.push_back({(float)(firstNoteTime + note.beat * beat), note.stringIndex, note.fret, note.pitch});
    }
    // Written down on the same clock: the chart's tick 0 is the first note, at this pass's tempo
    chart.offset = firstNoteTime;
    chart.tempoMap = {{0, (double)tempo}};
    score = buildScore(chart, chart.frettedTracks[0]);
    hits = perfects = 0;
}

void DrillExercise::finishPass(){
    int total = (int)notes.size();
    float accuracy = total > 0 ? 100.0f * hits / total : 0.0f;
    DrillPassOutcome outcome = finishDrillPass(config, progress, tempo, accuracy);
    if (outcome.clean) cleanPassesNow++;
    std::string error;
    saveDrillProgress(progressPath, progress, error);

    passText = TextFormat("Last pass at %d bpm: %d of %d notes (%.0f%%). ", tempo, hits, total, accuracy);
    if (outcome.newBest) passText += TextFormat("New best clean tempo! Next: %d bpm", outcome.nextTempo);
    else if (outcome.clean) passText += TextFormat("Clean. Next: %d bpm", outcome.nextTempo);
    else if (outcome.nextTempo < tempo) passText += TextFormat("Slowing down to %d bpm", outcome.nextTempo);
    else passText += TextFormat("Again at %d bpm (%d%% to speed up)", outcome.nextTempo, config.passPercent);
    startPass(); // straight into the next pass: practice keeps flowing
}

void DrillExercise::update(){
    if (ImGui::IsKeyPressed(ImGuiKey_Space)){
        running = !running;
        if (running) startPass();
        else stopPreviews();
    }
    if (noteInputActive() && !running) updateNoteInput(); // keep the input drained while paused

    if (!running) return;
    double now = audioTime();
    double beat = 60.0 / tempo;
    while (nextClick < totalClicks && countInStart + nextClick * beat < now + LOOKAHEAD_S){
        playClickAt(countInStart + nextClick * beat, nextClick % 4 == 0);
        nextClick++;
    }
    for (PlayNote& note : notes) if (note.hitFlash > 0.0f) note.hitFlash -= GetFrameTime();

    // Judging, the same as gameplay: keys by string, the instrument by pitch
    double t = drillTime();
    auto judge = [&](const PlayerInput& input){
        JudgeResult result = judgeInput(notes, input);
        if (result.judgement == Judgement::Ignored) return;
        hits += result.notesHit;
        if (result.judgement == Judgement::Perfect) perfects += result.notesHit;
    };
    int lanes = std::min((int)config.tuning.size(), MAX_KEY_LANES);
    for (int lane = 0; lane < lanes; lane++){
        if (!IsKeyPressed(KEY_ONE + lane)) continue;
        PlayerInput press;
        press.time = t;
        press.stringIndex = lane;
        judge(press);
    }
    if (noteInputActive()){
        for (const PlayedNote& played : updateNoteInput()){
            PlayerInput input;
            input.time = t - played.age - settings.inputOffsetMs / 1000.0;
            input.pitch = played.pitch;
            judge(input);
            lastPlayedPitch = played.pitch;
        }
    }
    markMisses(notes, t);
    if (now > passEndTime) finishPass();
}

void DrillExercise::draw(){
    const ScaleInfo* scale = findScale(config.scale);
    // Back sits in the top-left corner, out of the way of the text and the notes
    float textTop = ImGui::GetCursorPosY();
    ImGui::SetCursorPos(ImVec2(20, 20));
    if (ImGui::Button("Back")) leave = true;
    ImGui::SetCursorPosY(textTop);
    menuTitle(title.c_str());
    int shownTempo = running ? tempo : drillTempo(config, progress);
    centeredColoredText(TextFormat("%s in %s    %d bpm    best clean %d bpm    goal %d bpm", scale ? scale->displayName : "",
                                   pitchClassName(config.rootPitchClass), shownTempo, progress.bestCleanTempo, config.maxTempo), TEXT_DIM);
    if (!running){
        centeredText("Press Space to start. The metronome counts one bar in, then play along.");
    } else {
        double t = drillTime();
        if (t < firstNoteTime) centeredText("Get ready...");
        else centeredText(TextFormat("%d of %d notes hit (%d perfect)    Space to stop", hits, (int)notes.size(), perfects));
    }
    if (!passText.empty()) centeredColoredText(passText.c_str(), TEXT_GOOD);
    if (!inputError.empty()) centeredErrorText(inputError);
    if (noteInputActive()){
        centeredColoredText(lastPlayedPitch >= 0 ? TextFormat("Listening: you played %s%d", pitchClassName(lastPlayedPitch), pitchOctave(lastPlayedPitch))
                                                 : "Listening to your instrument", TEXT_DIM);
    } else {
        centeredColoredText("Keys 1 to 6 play the strings, lowest first", TEXT_DIM);
    }

    // The notes, in whichever views the settings choose, below the text
    float width = (float)GetScreenWidth(), height = (float)GetScreenHeight();
    TimeAxis axis = { (float)drillTime(), HIT_LINE_X, settings.noteSpeed };
    drawNoteViews({0, height * 0.48f, width, height * 0.51f}, settings.noteViews, notes, score, config.tuning, settings.lowStringOnTop, axis);
}
