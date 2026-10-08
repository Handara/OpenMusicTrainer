#include "learn/drillexercise.h"

#include "audio/audio.h"
#include "core/music.h"
#include "imgui.h"
#include "app/playerprogress.h"
#include "core/ranking.h"
#include "input/keynotes.h"
#include "input/keysinput.h"
#include "input/menuinput.h"
#include "input/noteinput.h"
#include "raylib.h"
#include "ui/ui.h"
#include "views/noteviews.h"
#include "ui/menulist.h"
#include "ui/neckcards.h"
#include "ui/particles.h"
#include "ui/scoreboard.h"
#include "ui/theme.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <map>

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
    band.setVolume(settings.bandVolume);
    piano = isPianoTuning(this->setup.tuning);
    if (piano){
        // A MIDI keyboard, else the computer's laid out as a piano from the C at or below the lowest note
        keysLow = setup.lowPitch, keysHigh = setup.highPitch;
        if (keysLow < 0 || keysHigh < 0){
            keysLow = 127, keysHigh = 0;
            for (const DrillNote& note : drillNotes) keysLow = std::min(keysLow, note.pitch), keysHigh = std::max(keysHigh, note.pitch);
            if (keysLow > keysHigh) keysLow = 60, keysHigh = 72;
        }
        startKeysInput(settings, keysLow);
    } else if (settings.playWithInstrument){
        float lowest = midiToFrequency((float)*std::min_element(setup.tuning.begin(), setup.tuning.end())) * 0.9f;
        int lowestPitch = *std::min_element(setup.tuning.begin(), setup.tuning.end());
        int channel = channelFor(settings, roleForTuning(lowestPitch)); // the bass's input for a bass drill
        if (!startNoteInput(settings.inputDevice, lowest, inputError, channel)) inputError = "Instrument: " + inputError + " (using the keyboard)";
    }
    ImGui::GetIO().ConfigFlags &= ~ImGuiConfigFlags_NavEnableKeyboard; // Space and the number keys play here
}

DrillExercise::~DrillExercise(){
    stopPreviews(); // clicks already handed to the audio engine would otherwise still play after leaving
    if (piano) stopKeysInput();
    else stopNoteInput();
    ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
}

bool DrillExercise::computerPiano() const {
    return piano && !keysInputIsMidi();
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
    combo = bestCombo = missedSoFar = 0;
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
    // In the player's journal: XP, achievements, how well each note was read
    Activity activity;
    activity.kind = ActivityKind::Drill;
    activity.id = std::filesystem::path(progressPath).stem().string();
    activity.title = title;
    const int lowest = *std::min_element(setup.tuning.begin(), setup.tuning.end());
    activity.instrument = isPianoTuning(setup.tuning) ? "piano" : lowest < 36 ? "bass" : "guitar";
    activity.seconds = (float)(passEndTime - countInStart);
    activity.right = hits;
    activity.total = total;
    activity.tempo = tempo;
    activity.clean = outcome.clean;
    activity.challenge = outcome.clean && challenge;
    activity.band = bandOn;
    activity.combo = bestCombo;
    // Its timing: how early the notes hit came on average (negative: late), and give or take how much
    std::vector<float> errors;
    for (const PlayNote& note : notes) if (note.hit) errors.push_back(note.error * 1000.0f);
    const TimingStats timing = timingStats(errors);
    endMeanMs = timing.meanMs;
    endSpreadMs = timing.unstableRate / 10.0f;
    endTimed = (int)errors.size();
    endCombo = bestCombo;
    if (!setup.timingOnly){ // a rhythm's notes are all the same: only their timing counts
        std::map<int, NoteTally> tallies;
        for (const PlayNote& note : notes){
            NoteTally& tally = tallies[note.pitch];
            tally.pitch = note.pitch;
            tally.asked++;
            if (note.hit) tally.right++;
        }
        for (const auto& [pitch, tally] : tallies) activity.notes.push_back(tally);
    }
    recordActivity(activity);
    history = recentRuns(activity.id, 12);
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
        // (not B: on the keyboard, B is a note; and Tab when the computer keyboard is a piano, its M a key)
        if (ImGui::IsKeyPressed(computerPiano() ? ImGuiKey_Tab : ImGuiKey_M, false)) toggleBand();
        // A piano's keys sound meanwhile: to find the notes before starting
        if (piano){
            for (const PlayedNote& played : updateKeysInput()) playKeysNote(midiToFrequency((float)played.pitch));
            if (clickedKey >= 0) playKeysNote(midiToFrequency((float)clickedKey));
            clickedKey = -1;
        }
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
            combo += result.notesHit;
            bestCombo = std::max(bestCombo, combo);
            comboAt = GetTime();
            burstDue = true;
        }
    };
    // Timing only (rhythm): every key and every note is "the" note, on the one string it's written on
    auto timingString = [&](){ return drillNotes.empty() ? 0 : drillNotes[0].stringIndex; };
    int lanes = piano ? 0 : std::min((int)setup.tuning.size(), MAX_KEY_LANES); // (a piano's notes are its keys)
    for (int lane = 0; lane < lanes; lane++){
        if (!IsKeyPressed(KEY_ONE + lane)) continue;
        PlayerInput press;
        press.time = t;
        press.stringIndex = setup.timingOnly ? timingString() : lane;
        judge(press);
    }
    // The keyboard's notes by name (input/keynotes), taken in the octave of the note due nearest now, and heard
    if (!setup.timingOnly && !computerPiano()){
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
            if (piano) playKeysNote(midiToFrequency((float)pitch));
            else playStringNote(midiToFrequency((float)pitch), bass, 0.6f, 0.7f);
            PlayerInput press;
            press.time = t;
            press.pitch = pitch;
            judge(press);
            lastPlayedPitch = pitch;
        }
    }
    // A piano's keys (and the keys clicked on screen), heard on the game's piano: most keyboards make no sound of their own
    if (piano){
        std::vector<PlayedNote> pressed = updateKeysInput();
        if (clickedKey >= 0) pressed.push_back({ clickedKey, 0.0f, 0.0 });
        clickedKey = -1;
        for (const PlayedNote& played : pressed){
            playKeysNote(midiToFrequency((float)played.pitch));
            PlayerInput input;
            input.time = t - played.age;
            input.pitch = played.pitch;
            judge(input);
            lastPlayedPitch = played.pitch;
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
    // A note gone by unplayed breaks the run of notes in a row
    const int missed = (int)std::count_if(notes.begin(), notes.end(), [](const PlayNote& note){ return note.judged && !note.hit; });
    if (missed > missedSoFar) combo = 0;
    missedSoFar = missed;
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
    const bool board = setup.showWhere || piano; // a neck or a piano's keys, between the text and the notes
    if (!board) centeredColoredText(setup.about.c_str(), uiColor(UiColor::Dim));
    if (!running){
        centeredText(menuInputActive() && !piano ? "Space or the open G string to start. Up and Down (the A and D strings): the tempo."
                                       : "Space to start, one bar counting in. Up and Down: the tempo.");
        const int challenge = drillChallengeTempo(setup.tempo);
        if (shownTempo < challenge)
            centeredColoredText(TextFormat("Practice: a clean pass at %d bpm or faster passes the drill", challenge), uiColor(UiColor::Dim));
        const char* bandKey = computerPiano() ? "Tab" : "M";
        centeredColoredText(bandOn ? TextFormat("With a %s band, its chords fitting your notes.  %s: the metronome alone", bandStyleName(bandStyle), bandKey)
                                   : TextFormat("With the metronome.  %s: a band instead", bandKey), uiColor(UiColor::Accent));
    } else {
        double t = drillTime();
        // How it's going: hit of the notes so far, out of the pass's
        const int sofar = (int)std::count_if(notes.begin(), notes.end(), [](const PlayNote& note){ return note.judged; });
        // ...and the band's chord now
        const int bar = (int)std::floor((t - firstNoteTime) / (60.0 / tempo) / setup.beatsPerBar);
        const std::string chord = bandOn && bar >= 0 && bar < (int)song.chords.size() ? "    " + song.chords[(size_t)bar] : "";
        if (t < firstNoteTime) centeredText("Get ready");
        else centeredText(TextFormat("%d of %d hit (out of %d)%s    Space to stop", hits, sofar, (int)notes.size(), chord.c_str()));
    }
    if (!passText.empty()) centeredColoredText(passText.c_str(), uiColor(UiColor::Good));
    if (!inputError.empty()) centeredErrorText(inputError);
    if (piano){
        centeredColoredText(keysInputIsMidi() ? "Play on your MIDI keyboard, or click the keys"
                                              : "No MIDI keyboard: the computer keyboard is the piano, each key's letter on it. Or click the keys",
                            uiColor(UiColor::Dim));
    } else if (noteInputActive()){
        centeredColoredText(lastPlayedPitch >= 0 ? TextFormat("Listening: you played %s%d", pitchClassName(lastPlayedPitch), pitchOctave(lastPlayedPitch))
                                                 : "Listening to your instrument", uiColor(UiColor::Dim));
    } else if (inputError.empty() || !setup.showWhere){
        centeredColoredText(setup.timingOnly ? "Any number key plays the note: it's the timing that counts" : KEYBOARD_NOTES_HINT, uiColor(UiColor::Dim));
    }

    // The notes, in whichever views the settings choose, below the text. Shown where: the neck above them, one panel
    // with the sheet music, over the top of its room (kept for notes far above the staff)
    float width = (float)GetScreenWidth(), height = (float)GetScreenHeight();
    const float notesTop = board ? height * 0.51f : height * 0.48f;
    if (board){
        DrawRectangleRec({ 0, height * 0.35f, width, notesTop - height * 0.35f }, themeColor(UiColor::Card));
        if (piano) drawKeys(width * 0.07f, width * 0.93f, height * 0.36f, notesTop - height * 0.01f, menuScale());
        else drawWhere(width * 0.07f, width * 0.93f, height * 0.35f, height * 0.57f, menuScale());
    }
    // Waiting, the pass stands still, shown as a moment before it starts
    TimeAxis axis = { running ? (float)drillTime() : (float)(WAITING_DOWNBEAT - WAITING_SHOWN_S), HIT_LINE_X, settings.noteSpeed };
    NoteViews views = settings.noteViews;
    if (setup.staffOnly){ views.staff = true; views.neck = false; }
    drawNoteViews({0, notesTop, width, height * 0.99f - notesTop}, views, notes, score, setup.tuning, settings.lowStringOnTop, axis);
    if (running){
        drawCountIn(menuScale());
        drawCombo(menuScale());
    }
    burstDue = false; // no neck to burst from: the hit's sparks pass
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
    if (burstDue && lastHit.stringIndex >= 0){ // sparks from the note just hit
        spawnBurst(ImVec2(board.fretX(lastHit.fret), board.stringY(lastHit.stringIndex)), uiColor(UiColor::Good), 14, 260.0f, s);
        burstDue = false;
    }
    if (lastHit.stringIndex >= 0 && since < HIT_RING_S){
        const float fade = 1.0f - since / HIT_RING_S;
        cardOutline(draw, ImVec2(board.fretX(lastHit.fret), board.stringY(lastHit.stringIndex)), halfW, halfH, 3 * s + 14 * s * since / HIT_RING_S,
                    uiColor(UiColor::Good, fade), 2.5f * s);
    }
}

// A piano's keyboard, where the neck is shown: the keys held down, the one just hit green a moment (sparks), the next
// to play lit (shown where); on the computer keyboard, each key's letter. A click on a key plays it.
void DrillExercise::drawKeys(float left, float right, float top, float bottom, float s){
    keys = pianoBoard(left, top, right - left, bottom - top, keysLow, keysHigh);
    if (burstDue && lastHit.pitch >= 0 && keys.shows(lastHit.pitch)){
        const ImVec4 key = keys.keyRect(lastHit.pitch);
        spawnBurst(ImVec2(key.x + key.z / 2, key.y + key.w * 0.75f), uiColor(UiColor::Good), 14, 260.0f, s);
        burstDue = false;
    }
    const PlayNote* next = nullptr;
    if (setup.showWhere) for (const PlayNote& note : notes) if (!note.judged){ next = &note; break; }
    const bool* down = keysInputDown();
    const float pulse = 0.5f + 0.5f * std::sin((float)GetTime() * 5.0f), since = (float)(GetTime() - hitAt);
    const int clicked = drawPianoBoard(keys, s, [&](int pitch){
        PianoKeyStyle look;
        const ImU32 own = pianoKeyColor(pitch - keys.firstPitch);
        if (next && next->pitch == pitch) look.fill = mixColor(own, uiColor(UiColor::Accent), 0.55f + 0.45f * pulse);
        if (down && pitch >= 0 && pitch < 128 && down[pitch]) look.fill = mixColor(own, uiColor(UiColor::Ink), 0.35f);
        if (lastHit.pitch == pitch && since < HIT_RING_S) look.fill = mixColor(own, uiColor(UiColor::Good), 1.0f - since / HIT_RING_S * 0.6f);
        look.label = keysInputLabel(pitch);
        return look;
    });
    if (clicked >= 0) clickedKey = clicked;
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
    const float grow = reducedMotion() ? 1.0f : 1.0f + 0.25f * std::exp(-(float)(GetTime() - endedAt) * 8.0f);
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
    // The best run of notes in a row, and the timing: early or late on average, and how steady
    y += 28 * s;
    std::string detail = TextFormat("Best run: %d in a row", endCombo);
    if (endTimed >= 4){
        const int mean = (int)std::lround(std::fabs(endMeanMs)), spread = (int)std::lround(endSpreadMs);
        detail += mean < 10 ? TextFormat("   ·   Timing: on the beat, give or take %d ms", spread)
                            : TextFormat("   ·   Timing: %d ms %s on average, give or take %d ms", mean, endMeanMs > 0 ? "early" : "late", spread);
    }
    draw->AddText(fonts.text, 16 * s, ImVec2(left, y), uiColor(UiColor::Dim), detail.c_str());
    if (endTimed >= 8 && std::fabs(endMeanMs) > 35.0f){
        y += 22 * s;
        draw->AddText(fonts.text, 15 * s, ImVec2(left, y), uiColor(UiColor::Accent, 0.8f),
                      endMeanMs < 0 ? "Always this late? Your instrument may need calibrating: Settings, Calibrate"
                                    : "Always this early? Your instrument may need calibrating: Settings, Calibrate");
    }
    drawHistory(width * 0.6f, height * 0.44f, width * 0.33f, height * 0.2f, s);

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
    for (size_t i = 0; i < choices.size() && !computerPiano(); i++){ // the shortcuts: N the next drill, C the challenge (not keys)
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

// The drill's last passes as a line: each pass's tempo, its dot lit when it was clean; the one just played ringed
void DrillExercise::drawHistory(float left, float top, float width, float height, float s){
    if (history.size() < 2) return;
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const UiFonts& fonts = uiFonts();
    draw->AddText(fonts.mono, 12 * s, ImVec2(left, top - 20 * s), uiColor(UiColor::Dim), TextFormat("YOUR LAST %d PASSES", (int)history.size()));
    draw->AddRectFilled(ImVec2(left, top), ImVec2(left + width, top + height), uiColor(UiColor::Card), 8 * s);
    int low = 1000, high = 0;
    for (const Activity& pass : history){
        low = std::min(low, pass.tempo);
        high = std::max(high, pass.tempo);
    }
    low -= 5;
    high += 5;
    const float pad = 16 * s;
    auto at = [&](size_t i){
        const float x = left + pad + (width - 2 * pad) * (float)i / (float)(history.size() - 1);
        const float y = top + height - pad - (height - 2 * pad) * (float)(history[i].tempo - low) / (float)std::max(1, high - low);
        return ImVec2(x, y);
    };
    for (size_t i = 1; i < history.size(); i++) draw->AddLine(at(i - 1), at(i), uiColor(UiColor::Accent, 0.5f), 2 * s);
    for (size_t i = 0; i < history.size(); i++){
        const bool clean = history[i].clean;
        draw->AddCircleFilled(at(i), 4.5f * s, uiColor(clean ? UiColor::Good : UiColor::Dim), 16);
        if (i + 1 == history.size()) draw->AddCircle(at(i), 8 * s, uiColor(UiColor::Ink), 20, 1.5f * s);
    }
    draw->AddText(fonts.mono, 11 * s, ImVec2(left + 6 * s, top + 4 * s), uiColor(UiColor::Dim), TextFormat("%d bpm", high - 5));
    draw->AddText(fonts.mono, 11 * s, ImVec2(left + 6 * s, top + height - 18 * s), uiColor(UiColor::Dim), TextFormat("%d bpm", low + 5));
    draw->AddText(fonts.text, 12 * s, ImVec2(left, top + height + 6 * s), uiColor(UiColor::Dim), "Each pass's tempo; green when clean");
}

// The count-in's beats, big over the notes: 4, 3, 2, 1, each popping in on its beat
void DrillExercise::drawCountIn(float s){
    const double beat = 60.0 / tempo, left = firstNoteTime - drillTime();
    if (left <= 0.0) return;
    const int number = (int)std::ceil(left / beat - 1e-6);
    const float within = (float)(1.0 - (left / beat - (number - 1))); // 0 as its beat comes, 1 as the next does
    const std::string text = std::to_string(number);
    ImDrawList* draw = ImGui::GetForegroundDrawList();
    ImFont* font = uiFonts().heavy;
    const float size = 150 * s * (1.0f + (reducedMotion() ? 0.0f : 0.25f) * std::max(0.0f, 1.0f - within * 4.0f));
    const ImVec2 extent = font->CalcTextSizeA(size, FLT_MAX, 0.0f, text.c_str());
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    draw->AddText(font, size, ImVec2((display.x - extent.x) / 2, display.y * 0.66f - extent.y / 2), uiColor(UiColor::Accent, 0.85f * (1.0f - within * 0.7f)),
                  text.c_str());
}

// The notes in a row, at the right under the scoreboard: from 3, popping as it grows, a ring at every tenth
void DrillExercise::drawCombo(float s){
    if (combo < 3) return;
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const UiFonts& fonts = uiFonts();
    const float since = (float)(GetTime() - comboAt), right = ImGui::GetWindowWidth() * 0.93f, top = ImGui::GetWindowHeight() * 0.235f;
    const bool milestone = combo % 10 == 0 && since < 0.6f;
    const float grow = reducedMotion() ? 0.0f : milestone ? 0.45f : 0.2f;
    const float size = 40 * s * (1.0f + grow * std::max(0.0f, 1.0f - since / 0.2f));
    const std::string text = std::to_string(combo);
    const ImVec2 extent = fonts.heavy->CalcTextSizeA(size, FLT_MAX, 0.0f, text.c_str());
    const ImVec2 at(right - extent.x, top);
    draw->AddText(fonts.heavy, size, at, uiColor(milestone ? UiColor::Good : UiColor::Accent), text.c_str());
    if (milestone && burstCombo != combo){ // once a milestone
        spawnBurst(ImVec2(at.x + extent.x / 2, at.y + extent.y / 2), uiColor(UiColor::Good), 24, 320.0f, s);
        burstCombo = combo;
    }
    if (milestone && !reducedMotion()){
        const ImVec2 c(at.x + extent.x / 2, at.y + extent.y / 2);
        draw->AddCircle(c, extent.y * (0.6f + since * 1.2f), uiColor(UiColor::Good, 1.0f - since / 0.6f), 40, 3 * s);
    }
    const ImVec2 label = fonts.mono->CalcTextSizeA(12 * s, FLT_MAX, 0.0f, "IN A ROW");
    draw->AddText(fonts.mono, 12 * s, ImVec2(right - label.x, top + extent.y + 2 * s), uiColor(UiColor::Dim), "IN A ROW");
}

bool DrillExercise::takeFinishedRun(int& percent){
    if (finishedPercent < 0) return false;
    percent = finishedPercent;
    finishedPercent = -1;
    return true;
}
