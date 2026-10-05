#include "learn/drillexercise.h"

#include "audio/audio.h"
#include "core/music.h"
#include "imgui.h"
#include "input/noteinput.h"
#include "raylib.h"
#include "ui/ui.h"
#include "views/noteviews.h"
#include "ui/menulist.h"
#include "ui/scoreboard.h"
#include "ui/theme.h"

#include <algorithm>
#include <cmath>

const double LEAD_IN_S = 0.3;      // silence before the count-in
const double LOOKAHEAD_S = 0.2;
const double WAITING_AHEAD_S = 1e5; // before the first pass, its notes wait this far ahead: off the screen    // metronome clicks are handed to the audio engine this far ahead
const float HIT_LINE_X = 180.0f;
const int MAX_KEY_LANES = 6;

DrillExercise::DrillExercise(const std::string& title, const DrillSetup& setup, const std::string& progressPath,
                             const Settings& settings)
    : title(title), setup(setup), progressPath(progressPath), settings(settings){
    progress = loadDrillProgress(progressPath);
    // The first pass, placed far ahead until it starts: the staff shows its clef, key and meter, but no notes yet
    drillNotes = this->setup.nextPass();
    chart = drillChart(drillNotes, setup.tuning, setup.key, setup.beatsPerBar);
    tempo = drillTempo(setup.tempo, progress);
    placePass(audioTime() + WAITING_AHEAD_S);
    if (settings.playWithInstrument){
        float lowest = midiToFrequency((float)*std::min_element(setup.tuning.begin(), setup.tuning.end())) * 0.9f;
        int lowestPitch = *std::min_element(setup.tuning.begin(), setup.tuning.end());
        int channel = channelFor(settings, roleForTuning(lowestPitch)); // the bass's input for a bass drill
        if (!startNoteInput(settings.inputDevice, lowest, inputError, channel)) inputError = "Instrument: " + inputError + " (using the keyboard)";
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

// Lays one pass out in audio time: a bar of count-in clicks, then the notes, clicked on every beat. The first pass
// plays the notes already shown; each one after gets its own (a rhythm drill's are new every time).
void DrillExercise::startPass(bool fresh){
    if (fresh){
        drillNotes = setup.nextPass();
        chart = drillChart(drillNotes, setup.tuning, setup.key, setup.beatsPerBar);
    }
    tempo = drillTempo(setup.tempo, progress);
    double beat = 60.0 / tempo;
    const int countIn = setup.beatsPerBar;
    countInStart = audioTime() + LEAD_IN_S;
    placePass(countInStart + countIn * beat);
    double lastBeat = drillNotes.empty() ? 0.0 : drillNotes.back().beat;
    passEndTime = firstNoteTime + lastBeat * beat + NEAR_WINDOW_S + 0.2;
    nextClick = 0;
    totalClicks = countIn + (int)std::ceil(lastBeat) + 1;
    hits = perfects = 0;
}

// The pass's notes and its written score, on the audio clock from `downbeat` (the first bar's first beat) at the
// current tempo. Always together: the staff finds each note of the score by its place in `notes`.
void DrillExercise::placePass(double downbeat){
    firstNoteTime = downbeat;
    double beat = 60.0 / tempo;
    notes.clear();
    for (const DrillNote& note : drillNotes){
        notes.push_back({(float)(downbeat + note.beat * beat), note.stringIndex, note.fret, note.pitch});
    }
    chart.offset = downbeat;
    chart.tempoMap = {{0, (double)tempo}};
    score = buildScore(chart, chart.frettedTracks[0]);
}

void DrillExercise::finishPass(){
    int total = (int)notes.size();
    float accuracy = total > 0 ? 100.0f * hits / total : 0.0f;
    DrillPassOutcome outcome = finishDrillPass(setup.tempo, progress, tempo, accuracy);
    if (outcome.clean) cleanPassesNow++;
    std::string error;
    saveDrillProgress(progressPath, progress, error);

    passText = TextFormat("Last pass at %d bpm: %d of %d notes (%.0f%%). ", tempo, hits, total, accuracy);
    if (outcome.newBest) passText += TextFormat("New best clean tempo! Next: %d bpm", outcome.nextTempo);
    else if (outcome.clean) passText += TextFormat("Clean. Next: %d bpm", outcome.nextTempo);
    else if (outcome.nextTempo < tempo) passText += TextFormat("Slowing down to %d bpm", outcome.nextTempo);
    else passText += TextFormat("Again at %d bpm (%d%% to speed up)", outcome.nextTempo, setup.tempo.passPercent);
    startPass(true); // straight into the next pass: practice keeps flowing
}

void DrillExercise::update(){
    if (ImGui::IsKeyPressed(ImGuiKey_Space)){
        running = !running;
        if (running) startPass(false);
        else stopPreviews();
    }
    if (noteInputActive() && !running) updateNoteInput(); // keep the input drained while paused

    if (!running) return;
    double now = audioTime();
    double beat = 60.0 / tempo;
    while (nextClick < totalClicks && countInStart + nextClick * beat < now + LOOKAHEAD_S){
        playClickAt(countInStart + nextClick * beat, nextClick % setup.beatsPerBar == 0);
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
    // Timing only (rhythm): every key and every note is "the" note, on the one string it's written on
    auto timingString = [&](){ return drillNotes.empty() ? 0 : drillNotes[0].stringIndex; };
    int lanes = std::min((int)setup.tuning.size(), MAX_KEY_LANES);
    for (int lane = 0; lane < lanes; lane++){
        if (!IsKeyPressed(KEY_ONE + lane)) continue;
        PlayerInput press;
        press.time = t;
        press.stringIndex = setup.timingOnly ? timingString() : lane;
        judge(press);
    }
    if (noteInputActive()){
        for (const PlayedNote& played : updateNoteInput()){
            PlayerInput input;
            input.time = t - played.age - settings.inputOffsetMs / 1000.0;
            if (setup.timingOnly) input.stringIndex = timingString();
            else input.pitch = played.pitch;
            judge(input);
            lastPlayedPitch = played.pitch;
        }
    }
    markMisses(notes, t);
    if (now > passEndTime) finishPass();
}

void DrillExercise::draw(){
    // Back sits in the top-right corner, out of the way of the title, the text and the notes
    float textTop = ImGui::GetCursorPosY();
    float backWidth = ImGui::CalcTextSize("Back").x + 2 * ImGui::GetStyle().FramePadding.x;
    ImGui::SetCursorPos(ImVec2(ImGui::GetWindowWidth() - backWidth - 20, 20));
    if (ImGui::Button("Back")) leave = true;
    ImGui::SetCursorPosY(textTop);
    menuTitle(title.c_str());
    int shownTempo = running ? tempo : drillTempo(setup.tempo, progress);
    drawScoreboard({
        { "TEMPO", TextFormat("%d", shownTempo), UiColor::Ink, "bpm" },
        { "BEST CLEAN", progress.bestCleanTempo > 0 ? std::string(TextFormat("%d", progress.bestCleanTempo)) : std::string("-"), UiColor::Accent, "bpm" },
        { "GOAL", TextFormat("%d", setup.tempo.maxTempo), progress.bestCleanTempo >= setup.tempo.maxTempo ? UiColor::Good : UiColor::Ink, "bpm" },
    }, ImGui::GetWindowWidth() * 0.93f, ImGui::GetWindowHeight() * 0.03f + 36 * menuScale(), menuScale());
    centeredColoredText(setup.about.c_str(), uiColor(UiColor::Dim));
    if (!running){
        centeredText("Press Space to start. The metronome counts one bar in, then play along.");
    } else {
        double t = drillTime();
        if (t < firstNoteTime) centeredText("Get ready...");
        else centeredText(TextFormat("%d of %d notes hit (%d perfect)    Space to stop", hits, (int)notes.size(), perfects));
    }
    if (!passText.empty()) centeredColoredText(passText.c_str(), uiColor(UiColor::Good));
    if (!inputError.empty()) centeredErrorText(inputError);
    if (noteInputActive()){
        centeredColoredText(lastPlayedPitch >= 0 ? TextFormat("Listening: you played %s%d", pitchClassName(lastPlayedPitch), pitchOctave(lastPlayedPitch))
                                                 : "Listening to your instrument", uiColor(UiColor::Dim));
    } else {
        centeredColoredText(setup.timingOnly ? "Any number key plays the note: it's the timing that counts"
                                             : "Keys 1 to 6 play the strings, lowest first", uiColor(UiColor::Dim));
    }

    // The notes, in whichever views the settings choose, below the text
    float width = (float)GetScreenWidth(), height = (float)GetScreenHeight();
    TimeAxis axis = { (float)drillTime(), HIT_LINE_X, settings.noteSpeed };
    NoteViews views = settings.noteViews;
    if (setup.staffOnly){ views.staff = true; views.neck = false; }
    drawNoteViews({0, height * 0.48f, width, height * 0.51f}, views, notes, score, setup.tuning, settings.lowStringOnTop, axis);
}
