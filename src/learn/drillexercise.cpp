#include "learn/drillexercise.h"

#include "audio/audio.h"
#include "core/music.h"
#include "imgui.h"
#include "input/keynotes.h"
#include "input/menuinput.h"
#include "input/noteinput.h"
#include "raylib.h"
#include "ui/ui.h"
#include "views/noteviews.h"
#include "ui/menulist.h"
#include "ui/neckcards.h"
#include "ui/scoreboard.h"
#include "ui/theme.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>

const double LEAD_IN_S = 0.3;      // silence before the count-in
const double LOOKAHEAD_S = 0.2;     // metronome clicks are handed to the audio engine this far ahead
// Waiting to start, the pass is placed at this time and shown as at a moment before it, standing still. Small
// numbers: the views place notes by subtracting times, and far ahead (once 1e5 s) a float's steps made them shake.
const double WAITING_DOWNBEAT = 1000.0;
const double WAITING_SHOWN_S = 1.0;
const float HIT_LINE_X = 180.0f;
const int MAX_KEY_LANES = 6;
const float HIT_RING_S = 0.6f;      // shown where: the ring round a note hit, on the neck
const int TEMPO_CHOICE_STEP = 5;    // Up and Down, waiting to start: the tempo chosen, this much at a time

// A name's number, the same on every machine (FNV-1a): a drill's id gives it its band's style
static unsigned stableHash(const std::string& text){
    unsigned hash = 2166136261u;
    for (unsigned char c : text) hash = (hash ^ c) * 16777619u;
    return hash;
}

DrillExercise::DrillExercise(const std::string& title, const DrillSetup& setup, const std::string& progressPath,
                             const Settings& settings)
    : title(title), setup(setup), progressPath(progressPath), settings(settings){
    progress = loadDrillProgress(progressPath);
    // The first pass, waiting to start: its first bar shown, to read ahead
    drillNotes = this->setup.nextPass();
    chart = drillChart(drillNotes, setup.tuning, setup.key, setup.beatsPerBar);
    tempo = drillTempo(setup.tempo, progress);
    placePass(WAITING_DOWNBEAT);
    // Its band: a style that suits its tempos, its own (from its id); the band or the metronome, as chosen last
    bandSeed = stableHash(std::filesystem::path(progressPath).filename().string());
    bandStyle = bandStyleFor(bandSeed, (setup.tempo.startTempo + std::max(setup.tempo.startTempo, setup.tempo.maxTempo)) / 2, setup.beatsPerBar);
    bandPath = (std::filesystem::path(progressPath).parent_path() / "drill-sound.txt").string();
    std::ifstream chosen(bandPath);
    std::string sound;
    bandOn = !(chosen >> sound) || sound != "metronome";
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
    hits = 0;
    // The band's song for these notes (its count-in the bar before), or the metronome's clicks
    if (bandOn){
        const int bars = std::max(1, (int)std::floor(lastBeat / setup.beatsPerBar) + 1);
        song = makeBandSong(drillNotes, setup.key, setup.beatsPerBar, bars, bandStyle, bandSeed + (unsigned)passNumber++);
        band.start(song, firstNoteTime, beat);
    } else band.stop();
}

void DrillExercise::toggleBand(){
    bandOn = !bandOn;
    std::ofstream out(bandPath);
    out << (bandOn ? "band" : "metronome") << "\n";
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

// The pass played to its end: judged and kept, then the end menu, the next pass's notes ready behind it
void DrillExercise::finishPass(){
    int total = (int)notes.size();
    float accuracy = total > 0 ? 100.0f * hits / total : 0.0f;
    // Only a pass at the challenge's tempo or faster counts for the drill (a course's score): slower is practice
    const bool challenge = tempo >= drillChallengeTempo(setup.tempo);
    finishedPercent = challenge ? (int)accuracy : -1;
    DrillPassOutcome outcome = finishDrillPass(setup.tempo, progress, tempo, accuracy);
    if (outcome.clean && challenge) cleanPassesNow++;
    std::string error;
    saveDrillProgress(progressPath, progress, error);
    endTempo = tempo;
    endHits = hits;
    endTotal = total;
    endOutcome = outcome;
    endedAt = GetTime();
    endMenu = MenuList{}; // its first row chosen: again
    band.schedule(band.endTime() + 1.0); // its ending, all of it: it rings out under the result
    stage = Stage::Ended;
    passText.clear();
    drillNotes = setup.nextPass();
    chart = drillChart(drillNotes, setup.tuning, setup.key, setup.beatsPerBar);
    tempo = drillTempo(setup.tempo, progress);
    placePass(WAITING_DOWNBEAT);
}

void DrillExercise::stopPass(){
    stopPreviews();
    band.stop();
    stage = Stage::Waiting;
    tempo = drillTempo(setup.tempo, progress);
    placePass(WAITING_DOWNBEAT); // the same notes, from their start
}

bool DrillExercise::takeNextChosen(){
    const bool chosen = nextChosen;
    nextChosen = false;
    return chosen;
}

void DrillExercise::update(){
    if (stage != Stage::Running){
        // Waiting: Space, or Enter (the instrument's choose, through the menus' listening) starts. The end menu
        // handles its own keys as it's drawn.
        if (ImGui::IsKeyPressed(ImGuiKey_M, false)) toggleBand(); // (not B: on the keyboard, B is a note)
        // Waiting, the tempo's the player's to choose (the instrument's A and D strings too): from the slowest the
        // drill goes to its goal
        if (stage == Stage::Waiting){
            const int change = ImGui::IsKeyPressed(ImGuiKey_UpArrow) ? TEMPO_CHOICE_STEP : ImGui::IsKeyPressed(ImGuiKey_DownArrow) ? -TEMPO_CHOICE_STEP : 0;
            if (change != 0){
                progress.tempo = std::clamp(drillTempo(setup.tempo, progress) + change, slowestDrillTempo(setup.tempo),
                                            std::max(setup.tempo.startTempo, setup.tempo.maxTempo));
                tempo = progress.tempo;
                std::string error;
                saveDrillProgress(progressPath, progress, error);
            }
        }
        if (stage == Stage::Waiting && (ImGui::IsKeyPressed(ImGuiKey_Space, false) || ImGui::IsKeyPressed(ImGuiKey_Enter, false)
                                        || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false))){
            stage = Stage::Running;
            startPass(false);
        }
        // The input kept drained meanwhile, unless the menus read it now (input/menuinput)
        if (noteInputActive() && !menuInputActive()) updateNoteInput();
        return;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Space, false)){ // not repeated: held, it would stop what it started
        stopPass();
        return;
    }
    double now = audioTime();
    double beat = 60.0 / tempo;
    if (bandOn) band.schedule(now + LOOKAHEAD_S);
    else while (nextClick < totalClicks && countInStart + nextClick * beat < now + LOOKAHEAD_S){
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
        if (result.notesHit > 0 && result.noteIndex >= 0){
            lastHit = notes[result.noteIndex];
            hitAt = GetTime();
        }
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
    // The keyboard's notes by name (input/keynotes), taken in the octave of the note due nearest now, and heard
    if (!setup.timingOnly){
        const int keyClass = keyboardNoteClass();
        if (keyClass >= 0){
            int expected = -1;
            double nearest = 1e9;
            for (const PlayNote& note : notes){
                if (note.judged || std::abs(note.time - t) >= nearest) continue;
                nearest = std::abs(note.time - t);
                expected = note.pitch;
            }
            const int pitch = nearestPitchOfClass(keyClass, expected >= 0 ? expected : 60);
            const bool bass = *std::min_element(setup.tuning.begin(), setup.tuning.end()) < 36;
            playStringNote(midiToFrequency((float)pitch), bass, 0.6f, 0.7f);
            PlayerInput press;
            press.time = t;
            press.pitch = pitch;
            judge(press);
            lastPlayedPitch = pitch;
        }
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
    menuTitle(title.c_str()); // (back: Esc, or the Back button at the top left)
    const bool running = stage == Stage::Running;
    int shownTempo = running ? tempo : drillTempo(setup.tempo, progress);
    drawScoreboard({
        { "TEMPO", TextFormat("%d", shownTempo), UiColor::Ink, "bpm" },
        { "BEST CLEAN", progress.bestCleanTempo > 0 ? std::string(TextFormat("%d", progress.bestCleanTempo)) : std::string("-"), UiColor::Accent, "bpm" },
        { "TO PASS", TextFormat("%d", drillChallengeTempo(setup.tempo)), progress.bestCleanTempo >= drillChallengeTempo(setup.tempo) ? UiColor::Good : UiColor::Ink,
          progress.bestCleanTempo >= drillChallengeTempo(setup.tempo) ? "bpm: passed" : "bpm, clean" },
    }, ImGui::GetWindowWidth() * 0.93f, ImGui::GetWindowHeight() * 0.03f + 36 * menuScale(), menuScale());
    if (stage == Stage::Ended){
        drawEnd(menuScale());
        return;
    }
    // (shown where: the title says what's read, and an input's error takes its line's place, for the neck's room)
    if (!setup.showWhere) centeredColoredText(setup.about.c_str(), uiColor(UiColor::Dim));
    if (!running){
        centeredText(menuInputActive() ? "Space or the open G string to start. Up and Down (the A and D strings): the tempo."
                                       : "Space to start, one bar counting in. Up and Down: the tempo.");
        const int challenge = drillChallengeTempo(setup.tempo);
        if (shownTempo < challenge)
            centeredColoredText(TextFormat("Practice: a clean pass at %d bpm or faster passes the drill", challenge), uiColor(UiColor::Dim));
        centeredColoredText(bandOn ? TextFormat("With a %s band, its chords fitting your notes.  M: the metronome alone", bandStyleName(bandStyle))
                                   : "With the metronome.  M: a band instead", uiColor(UiColor::Accent));
    } else {
        double t = drillTime();
        // How it's going: hit of the notes so far, out of the pass's
        const int sofar = (int)std::count_if(notes.begin(), notes.end(), [](const PlayNote& note){ return note.judged; });
        // ...and the band's chord now
        const int bar = (int)std::floor((t - firstNoteTime) / (60.0 / tempo) / setup.beatsPerBar);
        const std::string chord = bandOn && bar >= 0 && bar < (int)song.chords.size() ? "    " + song.chords[(size_t)bar] : "";
        if (t < firstNoteTime) centeredText("Get ready...");
        else centeredText(TextFormat("%d of %d hit (out of %d)%s    Space to stop", hits, sofar, (int)notes.size(), chord.c_str()));
    }
    if (!passText.empty()) centeredColoredText(passText.c_str(), uiColor(UiColor::Good));
    if (!inputError.empty()) centeredErrorText(inputError);
    if (noteInputActive()){
        centeredColoredText(lastPlayedPitch >= 0 ? TextFormat("Listening: you played %s%d", pitchClassName(lastPlayedPitch), pitchOctave(lastPlayedPitch))
                                                 : "Listening to your instrument", uiColor(UiColor::Dim));
    } else if (inputError.empty() || !setup.showWhere){
        centeredColoredText(setup.timingOnly ? "Any number key plays the note: it's the timing that counts" : KEYBOARD_NOTES_HINT, uiColor(UiColor::Dim));
    }

    // The notes, in whichever views the settings choose, below the text. Shown where: the neck above them, one panel
    // with the sheet music, over the top of its room (kept for notes far above the staff)
    float width = (float)GetScreenWidth(), height = (float)GetScreenHeight();
    const float notesTop = setup.showWhere ? height * 0.51f : height * 0.48f;
    if (setup.showWhere){
        DrawRectangleRec({ 0, height * 0.35f, width, notesTop - height * 0.35f }, themeColor(UiColor::Card));
        drawWhere(width * 0.07f, width * 0.93f, height * 0.35f, height * 0.57f, menuScale());
    }
    // Waiting, the pass stands still, shown as a moment before it starts
    TimeAxis axis = { running ? (float)drillTime() : (float)(WAITING_DOWNBEAT - WAITING_SHOWN_S), HIT_LINE_X, settings.noteSpeed };
    NoteViews views = settings.noteViews;
    if (setup.staffOnly){ views.staff = true; views.neck = false; }
    drawNoteViews({0, notesTop, width, height * 0.99f - notesTop}, views, notes, score, setup.tuning, settings.lowStringOnTop, axis);
}

// The neck, as a course's untimed drills have it: the next note to play lit, pulsing; a note just hit, a green ring
void DrillExercise::drawWhere(float left, float right, float top, float bottom, float s){
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const int strings = (int)setup.tuning.size();
    int highest = 12;
    for (const DrillNote& note : drillNotes) highest = std::max(highest, note.fret);
    // The board's padding and fret numbers take about two strings' room: the strings get the rest
    const float spacing = std::min(30.0f, (bottom - top) / s / (float)(strings + 1));
    const FretboardLayout board = fretboardLayout(left, top, right - left, s, strings, 0, highest, spacing);
    drawFretboard(board, setup.tuning);
    float halfW, halfH;
    cardSize(board, halfW, halfH);
    auto next = std::find_if(notes.begin(), notes.end(), [](const PlayNote& note){ return !note.judged; });
    if (next != notes.end()){
        const float pulse = 0.5f + 0.5f * std::sin((float)GetTime() * 5.0f);
        drawNoteCard(draw, board, next->stringIndex, next->fret, next->pitch, 2 * s * pulse, 1.0f, true, s);
        cardOutline(draw, ImVec2(board.fretX(next->fret), board.stringY(next->stringIndex)), halfW, halfH, 4 * s + 4 * s * pulse,
                    uiColor(UiColor::Accent, 0.6f), 2 * s);
    }
    const float since = (float)(GetTime() - hitAt);
    if (lastHit.stringIndex >= 0 && since < HIT_RING_S){
        const float fade = 1.0f - since / HIT_RING_S;
        cardOutline(draw, ImVec2(board.fretX(lastHit.fret), board.stringY(lastHit.stringIndex)), halfW, halfH, 3 * s + 14 * s * since / HIT_RING_S,
                    uiColor(UiColor::Good, fade), 2.5f * s);
    }
}

// How the pass went, big, then what to do: again (at the tempo it earned), the other tempo, the course's next drill,
// back. Chosen with the arrows and Enter, the mouse, or the instrument (its strings, or each row's own note).
// How the pass went, big, then what to do. Clean but slower than the challenge: faster, or the challenge itself.
// Passed at the challenge: on to the next drill, or faster still. Not clean: again a little slower, or the same. Chosen
// with the arrows and Enter, the mouse, or the instrument (its strings, or each row's own note).
void DrillExercise::drawEnd(float s){
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const UiFonts& fonts = uiFonts();
    const float width = ImGui::GetWindowWidth(), height = ImGui::GetWindowHeight(), left = width * 0.07f;
    const bool clean = endOutcome.clean;
    const int challenge = drillChallengeTempo(setup.tempo);
    const bool atChallenge = endTempo >= challenge, passed = clean && atChallenge;
    const int percent = endTotal > 0 ? endHits * 100 / endTotal : 0, toPass = (endTotal * setup.tempo.passPercent + 99) / 100;
    const float grow = 1.0f + 0.25f * std::exp(-(float)(GetTime() - endedAt) * 8.0f);
    float y = height * 0.19f;
    draw->AddText(fonts.heavy, 54 * s * grow, ImVec2(left, y), uiColor(clean ? UiColor::Good : UiColor::Ink), passed ? "Passed!" : clean ? "Clean!" : "Not yet");
    y += 70 * s;
    draw->AddText(fonts.bold, 20 * s, ImVec2(left, y), uiColor(UiColor::Ink),
                  TextFormat("%d of %d notes at %d bpm: %d%%  (%d to pass)", endHits, endTotal, endTempo, percent, toPass));
    y += 30 * s;
    const char* line = passed ? (endOutcome.newBest ? "New best clean tempo!" : "")
                     : clean ? TextFormat("Now the challenge: clean at %d bpm passes the drill", challenge)
                     : atChallenge ? "A little slower to get it clean, then back up" : "";
    draw->AddText(fonts.bold, 18 * s, ImVec2(left, y), uiColor(UiColor::Accent), line);

    // The choices
    const int next = drillTempo(setup.tempo, progress); // what the pass earned: 5 faster when clean, else 5 slower
    const int faster = std::min(setup.tempo.maxTempo, endTempo + TEMPO_CHOICE_STEP);
    enum class Choice { Play, Next, Back };
    std::vector<MenuRow> rows;
    std::vector<std::pair<Choice, int>> choices; // with the tempo it plays at
    auto add = [&](const std::string& label, Choice choice, int at, const std::string& key = ""){
        rows.push_back(actionRow(label, key));
        choices.push_back({ choice, at });
    };
    if (passed){
        if (!nextLabel.empty()) add(nextLabel, Choice::Next, 0, "N");
        if (faster > endTempo) add(TextFormat("Faster: %d bpm", faster), Choice::Play, faster, nextLabel.empty() ? "Space" : "");
        add(TextFormat("Again: %d bpm", endTempo), Choice::Play, endTempo);
    } else if (clean){
        if (faster > endTempo) add(TextFormat("Faster: %d bpm", faster), Choice::Play, faster, "Space");
        add(TextFormat("Try the passing challenge: %d bpm", challenge), Choice::Play, challenge, "C");
        add(TextFormat("Same tempo: %d bpm", endTempo), Choice::Play, endTempo);
        if (!nextLabel.empty()) add(nextLabel, Choice::Next, 0, "N");
    } else {
        add(next < endTempo ? TextFormat("Again, slower: %d bpm", next) : TextFormat("Again: %d bpm", next), Choice::Play, next, "Space");
        if (next != endTempo) add(TextFormat("Same tempo: %d bpm", endTempo), Choice::Play, endTempo);
        if (endTempo != challenge) add(TextFormat("Try the passing challenge: %d bpm", challenge), Choice::Play, challenge, "C");
        if (!nextLabel.empty()) add(nextLabel, Choice::Next, 0, "N");
    }
    add("Back", Choice::Back, 0, "Esc");
    int picked = menuList(endMenu, rows, { ImVec2(left, height * 0.42f), width * 0.5f, height * 0.45f, s });
    for (size_t i = 0; i < choices.size(); i++){ // the shortcuts: N the next drill, C the challenge
        if (ImGui::IsKeyPressed(ImGuiKey_N, false) && choices[i].first == Choice::Next) picked = (int)i;
        if (ImGui::IsKeyPressed(ImGuiKey_C, false) && rows[i].key == "C") picked = (int)i;
    }
    if (picked >= 0 && picked < (int)choices.size()){
        const auto [choice, at] = choices[(size_t)picked];
        switch (choice){
            case Choice::Play: {
                progress.tempo = at;
                std::string error;
                saveDrillProgress(progressPath, progress, error);
                stage = Stage::Running;
                startPass(false); // the notes ready behind the menu
                break;
            }
            case Choice::Next: nextChosen = true; break;
            case Choice::Back: leave = true; break;
        }
    }
    menuScreenHint("Enter choose    Esc back", s);
    ImGui::Dummy(ImVec2(1, 1)); // the list moved ImGui's cursor: an item after it
}

bool DrillExercise::takeFinishedRun(int& percent){
    if (finishedPercent < 0) return false;
    percent = finishedPercent;
    finishedPercent = -1;
    return true;
}
