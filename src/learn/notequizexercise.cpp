#include "learn/notequizexercise.h"

#include "audio/audio.h"
#include "core/drill.h"
#include "core/music.h"
#include "core/synth.h"
#include "imgui.h"
#include "input/noteinput.h"
#include "raylib.h"
#include "ui/menulist.h"
#include "ui/neckcards.h"
#include "ui/scoreboard.h"
#include "ui/theme.h"
#include "ui/ui.h"
#include "views/staff.h"

#include <algorithm>
#include <cfloat>
#include <cmath>

const float RIGHT_FLASH_S = 0.6f;  // the ring round a note played right
const float WRONG_SHOWN_S = 2.5f;  // what was played instead, and where the right one is
const float CHEER_S = 1.6f;
const float CHEER_VOLUME = 0.7f;
const int STAFF_NOTES_A_BAR = 4;
const float STAFF_EASE = 8.0f;     // how fast the staff follows to the note now
const double FIRST_PROMPT_S = 0.6; // by ear: the first note asked plays this long after the run starts
const double NEXT_PROMPT_S = 0.9;  //   the next, this long after a right one (it rings out first)
const float REFERENCE_S = 0.8f;    //   the reference note, then the one asked
const float PROMPT_S = 1.4f;
const float PROMPT_VOLUME = 0.85f;

// "an E", "a G": a note's name with its article, as it's said
static std::string withArticle(const char* name){
    return std::string(name[0] == 'A' || name[0] == 'E' || name[0] == 'F' ? "an " : "a ") + name;
}

NoteQuizExercise::NoteQuizExercise(const std::string& title, const NoteQuizConfig& config, const KeySignature& key, bool onBass,
                                   const std::string& progressPath, const Settings& settings)
    : title(title), config(config), key(key), onBass(onBass), progressPath(progressPath), settings(settings),
      random(std::random_device{}()){
    stats = loadNoteQuizStats(progressPath);
    const int rate = std::max(1, audioSampleRate());
    cheer.resize((size_t)(CHEER_S * rate));
    renderCrowd(cheer.data(), (int)cheer.size(), rate, CrowdReaction::Cheer, 2);
    const InputRole role = onBass ? InputRole::Bass : InputRole::Guitar;
    listening = startNoteInput(settings.inputDevice, midiToFrequency((float)config.tuning.front()) * 0.9f, inputError, channelFor(settings, role));
    ImGui::GetIO().ConfigFlags &= ~ImGuiConfigFlags_NavEnableKeyboard; // Space starts a run again here
    startRun();
}

NoteQuizExercise::~NoteQuizExercise(){
    stopNoteInput();
    ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
}

// A new run, and its notes written down for the staff: four a bar, a beat each (at 60 bpm, a beat is a second)
void NoteQuizExercise::startRun(){
    startNoteQuiz(run, noteQuizPrompts(config, random));
    finished = passed = false;
    rightAt = wrongAt = -100.0;
    lastRight = { -1, -1, -1 };
    std::vector<DrillNote> notes;
    for (size_t i = 0; i < run.prompts.size(); i++) notes.push_back({ (double)i, run.prompts[i].string, run.prompts[i].fret, run.prompts[i].pitch });
    chart = drillChart(notes, config.tuning, key, STAFF_NOTES_A_BAR);
    if (onBass) chart.frettedTracks[0].type = InstrumentType::Bass; // the bass clef
    score = buildScore(chart, chart.frettedTracks[0]);
    staffNotes.clear();
    for (const DrillNote& note : notes){
        PlayNote play{ (float)note.beat, note.stringIndex, note.fret, note.pitch };
        play.beats = 1.0f;
        staffNotes.push_back(play);
    }
    shownTime = 0.0f;
    if (config.prompt == NotePrompt::Ear) playPromptAt = GetTime() + FIRST_PROMPT_S;
}

void NoteQuizExercise::playPrompt(){
    if (run.next >= run.prompts.size()) return;
    double at = audioTime() + 0.05;
    const double start = at;
    if (config.reference >= 0){
        playStringNoteAt(midiToFrequency((float)config.reference), onBass, REFERENCE_S, at, PROMPT_VOLUME);
        at += REFERENCE_S;
    }
    playStringNoteAt(midiToFrequency((float)run.prompts[run.next].pitch), onBass, PROMPT_S, at, PROMPT_VOLUME);
    heardAt = GetTime() + (at - start);
    soundingUntil = GetTime() + (at - start) + PROMPT_S * 0.6;
}

void NoteQuizExercise::played(int pitch){
    if (finished) return;
    if (config.prompt == NotePrompt::Ear && GetTime() < soundingUntil) return; // lahn's own note, heard through a microphone
    const size_t asked = run.next;
    if (playNoteQuiz(run, config, pitch)){
        rightAt = GetTime();
        lastRight = run.prompts[asked];
        if (asked < staffNotes.size()){
            staffNotes[asked].judged = true;
            staffNotes[asked].hit = run.firstTime.back();
        }
        playHitSound(true);
        if (noteQuizDone(run)) finish();
        else if (config.prompt == NotePrompt::Ear) playPromptAt = GetTime() + NEXT_PROMPT_S;
    } else {
        wrongAt = GetTime();
    }
}

void NoteQuizExercise::finish(){
    finished = true;
    finishedAt = GetTime();
    passed = noteQuizPassed(run, config);
    finishedPercent = run.prompts.empty() ? 0 : noteQuizRight(run) * 100 / (int)run.prompts.size();
    stats.runs++;
    if (passed){
        stats.passed++;
        passedNow++;
        playSamplesAt(cheer, audioTime(), CHEER_VOLUME);
    }
    std::string error;
    if (!saveNoteQuizStats(progressPath, stats, error)) TraceLog(LOG_WARNING, "Progress: %s", error.c_str());
}

void NoteQuizExercise::update(){
    if (listening) for (const PlayedNote& note : updateNoteInput()) played(note.pitch);
    if (finished && ImGui::IsKeyPressed(ImGuiKey_Space, false)) startRun();
    else if (config.prompt == NotePrompt::Ear && !finished && (ImGui::IsKeyPressed(ImGuiKey_Space, false) || ImGui::IsKeyPressed(ImGuiKey_R, false)))
        playPrompt(); // hear it again
    if (playPromptAt >= 0.0 && GetTime() >= playPromptAt){
        playPromptAt = -1.0;
        playPrompt();
    }
    // The staff follows to the note now
    const float target = staffNotes.empty() ? 0.0f : staffNotes[std::min(run.next, staffNotes.size() - 1)].time;
    shownTime += (target - shownTime) * std::min(1.0f, GetFrameTime() * STAFF_EASE);
}

// What to play, said plainly
std::string NoteQuizExercise::promptText() const {
    if (run.next >= run.prompts.size()) return "";
    const NeckStep& note = run.prompts[run.next];
    switch (config.prompt){
        case NotePrompt::Neck: return "Play " + notePlaceText(note, config.tuning);
        case NotePrompt::Name: return "Play " + withArticle(pitchClassName(note.pitch));
        case NotePrompt::Staff: return "Play the note that's lit on the staff";
        case NotePrompt::Ear: return "Listen, then play it back";
    }
    return "";
}

// The run's notes as dots: done (green right the first time, red after a slip), the one now, the ones to come
void NoteQuizExercise::drawProgress(float left, float right, float top, float s){
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const int count = (int)run.prompts.size();
    if (count == 0) return;
    const float gap = std::min(34 * s, (right - left) / count), radius = 8 * s;
    for (int i = 0; i < count; i++){
        const ImVec2 at(left + radius + i * gap, top);
        if (i < (int)run.firstTime.size()){
            const float since = i + 1 == (int)run.firstTime.size() ? (float)(GetTime() - rightAt) : 99.0f;
            const float pop = since < 0.25f ? 4 * s * (1.0f - since / 0.25f) : 0.0f;
            draw->AddCircleFilled(at, radius + pop, uiColor(run.firstTime[i] ? UiColor::Good : UiColor::Bad), 24);
        } else if (i == (int)run.next){
            draw->AddCircle(at, radius + 2 * s, uiColor(UiColor::Accent), 24, 2.5f * s);
        } else {
            draw->AddCircle(at, radius, uiColor(UiColor::Ink, 0.35f), 24, 2 * s);
        }
    }
}

void NoteQuizExercise::drawStaffPrompts(float left, float top, float width, float height){
    TimeAxis axis;
    axis.songTime = shownTime;
    axis.hitLineX = left;
    axis.noteSpeed = 100.0f;
    drawStaff({ left, top, width, height }, staffNotes, score, axis);
}

// Play mode's neck: the note asked, when it's shown there (and after a slip, always: where it is), pulsing; the note
// just played right, a green ring
void NoteQuizExercise::drawNeck(float left, float right, float top, float bottom, float s){
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const int strings = (int)config.tuning.size();
    int highest = 12;
    for (const NeckStep& note : config.notes) highest = std::max(highest, note.fret);
    const float spacing = std::min(42.0f, (bottom - top) / s / (float)strings);
    board = fretboardLayout(left, top, right - left, s, strings, 0, highest, spacing);
    drawFretboard(board, config.tuning);
    float halfW, halfH;
    cardSize(board, halfW, halfH);
    // The notes it could be, outlined: which one is it?
    if (config.candidates && !finished){
        for (size_t i = 0; i < config.notes.size(); i++){
            bool seen = false;
            for (size_t j = 0; j < i; j++) seen = seen || (config.notes[j].string == config.notes[i].string && config.notes[j].fret == config.notes[i].fret);
            if (!seen) cardOutline(draw, ImVec2(board.fretX(config.notes[i].fret), board.stringY(config.notes[i].string)), halfW, halfH, 0.0f,
                                   uiColor(UiColor::Ink, 0.35f), 1.5f * s);
        }
    }
    if (!finished && run.next < run.prompts.size()){
        const NeckStep& note = run.prompts[run.next];
        const bool shown = config.prompt == NotePrompt::Neck || config.showWhere || run.slipped;
        if (shown){
            const float pulse = 0.5f + 0.5f * std::sin((float)GetTime() * 5.0f);
            drawNoteCard(draw, board, note.string, note.fret, note.pitch, 2 * s * pulse, 1.0f, true, s);
            cardOutline(draw, ImVec2(board.fretX(note.fret), board.stringY(note.string)), halfW, halfH, 4 * s + 4 * s * pulse,
                        uiColor(UiColor::Accent, 0.6f), 2 * s);
        }
    }
    const float since = (float)(GetTime() - rightAt);
    if (lastRight.string >= 0 && since < RIGHT_FLASH_S){
        const float fade = 1.0f - since / RIGHT_FLASH_S;
        drawNoteCard(draw, board, lastRight.string, lastRight.fret, lastRight.pitch, 0.0f, fade, false, s);
        cardOutline(draw, ImVec2(board.fretX(lastRight.fret), board.stringY(lastRight.string)), halfW, halfH, 3 * s + 14 * s * since / RIGHT_FLASH_S,
                    uiColor(UiColor::Good, fade), 2.5f * s);
    }
}

void NoteQuizExercise::draw(){
    menuTitle(title.c_str());
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const UiFonts& fonts = uiFonts();
    const float s = menuScale(), width = ImGui::GetWindowWidth(), height = ImGui::GetWindowHeight();
    const float left = width * 0.07f, right = width * 0.93f;
    drawScoreboard({
        { "RIGHT", TextFormat("%d", noteQuizRight(run)), UiColor::Good, TextFormat("of %d, first time", (int)run.prompts.size()) },
        { "SLIPS", TextFormat("%d", run.mistakes), run.mistakes == 0 ? UiColor::Ink : UiColor::Bad, "" },
        { "PASSED", TextFormat("%d", stats.passed), UiColor::Accent, TextFormat("of %d %s", stats.runs, stats.runs == 1 ? "run" : "runs") },
    }, right, height * 0.03f + 36 * s, s);

    drawProgress(left, right, height * 0.215f, s);

    // What to play: the instruction, big (and for a name or the staff, the note's name or the staff itself)
    const float promptTop = height * 0.26f;
    float neckTop = height * 0.45f;
    if (!finished){
        if (config.prompt == NotePrompt::Staff){
            drawStaffPrompts(left, promptTop + 30 * s, right - left, height * 0.18f);
            neckTop = height * 0.56f;
        } else if (config.prompt == NotePrompt::Name && run.next < run.prompts.size()){
            draw->AddText(fonts.heavy, 72 * s, ImVec2(left, promptTop - 8 * s), uiColor(UiColor::Ink), pitchClassName(run.prompts[run.next].pitch));
        }
        if (config.prompt == NotePrompt::Ear){
            // A ring going out as the note sounds
            const float since = (float)(GetTime() - heardAt);
            const ImVec2 at(left + 30 * s, promptTop + 30 * s);
            draw->AddCircleFilled(at, 18 * s, uiColor(UiColor::Accent, since >= 0.0f && since < 1.2f ? 1.0f : 0.5f), 32);
            if (since >= 0.0f && since < 1.2f) draw->AddCircle(at, 18 * s + 30 * s * since, uiColor(UiColor::Accent, 1.0f - since / 1.2f), 40, 2.5f * s);
            const std::string again = config.reference >= 0 ? std::string("Space: hear it again (first ") + pitchClassName(config.reference) + ", then the note)"
                                                            : "Space: hear it again";
            draw->AddText(fonts.text, 17 * s, ImVec2(left + 64 * s, promptTop + 52 * s), uiColor(UiColor::Dim), again.c_str());
        }
        const std::string text = promptText();
        const float textY = config.prompt == NotePrompt::Staff ? promptTop - 2 * s : config.prompt == NotePrompt::Name ? promptTop + 84 * s : promptTop + 10 * s;
        const float textX = config.prompt == NotePrompt::Ear ? left + 64 * s : left;
        draw->AddText(fonts.bold, (config.prompt == NotePrompt::Neck || config.prompt == NotePrompt::Ear ? 32 : 22) * s, ImVec2(textX, textY),
                      uiColor(UiColor::Ink), text.c_str());
        // After a slip: what it was, and where the right one is
        const float wrongSince = (float)(GetTime() - wrongAt);
        if (run.slipped && wrongSince < WRONG_SHOWN_S && run.next < run.prompts.size()){
            const NeckStep& asked = run.prompts[run.next];
            const std::string where = notePlaceText(asked, config.tuning);
            const std::string what = (run.lastWrong - asked.pitch) % 12 == 0 ? "Right note, wrong octave: this one is " + where + "."
                                   : "That was " + withArticle(pitchClassName(run.lastWrong)) + ". It's " + where + ": try again.";
            const float y = config.prompt == NotePrompt::Staff ? neckTop - 26 * s : textY + 44 * s;
            draw->AddText(fonts.bold, 18 * s, ImVec2(left, y), uiColor(UiColor::Bad, std::min(1.0f, (WRONG_SHOWN_S - wrongSince) * 2.0f)), what.c_str());
        }
    }
    const float neckBottom = neckTop + std::min(42.0f * s * (float)config.tuning.size(), height * 0.34f);
    drawNeck(left, right, neckTop, neckBottom, s);
    // A click on a fret plays it: for trying it out without an instrument
    const ImVec2 mouse = ImGui::GetMousePos();
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && mouse.y >= board.top && mouse.y <= board.top + board.height){
        const int fret = board.fretAt(mouse.x), string = board.stringAt(mouse.y);
        if (fret >= 0 && string >= 0 && string < (int)config.tuning.size()){
            playStringNote(midiToFrequency((float)(config.tuning[string] + fret)), onBass, 1.0f, 0.8f);
            played(config.tuning[string] + fret);
        }
    }

    // The run's end: passed, or not yet
    if (finished){
        const float grow = 1.0f + 0.3f * std::exp(-(float)(GetTime() - finishedAt) * 8.0f);
        draw->AddText(fonts.heavy, 54 * s * grow, ImVec2(left, promptTop - 10 * s), uiColor(passed ? UiColor::Good : UiColor::Ink),
                      passed ? "Passed!" : "Nearly");
        draw->AddText(fonts.bold, 20 * s, ImVec2(left, promptTop + 62 * s), uiColor(UiColor::Ink),
                      TextFormat("%d of %d right the first time (%d to pass).  Space to go again.", noteQuizRight(run), (int)run.prompts.size(),
                                 std::min(config.pass, (int)run.prompts.size())));
    }
    float textY = neckBottom + 36 * s;
    if (!inputError.empty()) draw->AddText(fonts.text, 16 * s, ImVec2(left, textY), uiColor(UiColor::Bad), inputError.c_str());
    else if (!listening) draw->AddText(fonts.text, 16 * s, ImVec2(left, textY), uiColor(UiColor::Dim), "No instrument: click the frets to play");
    menuScreenHint(finished ? "Space  again    Esc  back" : config.prompt == NotePrompt::Ear ? "Space  hear it again    Esc  back" : "Esc  back", s);
    ImGui::Dummy(ImVec2(1, 1)); // the board moved ImGui's cursor (ui/fretboardview): an item after it
}

bool NoteQuizExercise::takeFinishedRun(int& percent){
    if (finishedPercent < 0) return false;
    percent = finishedPercent;
    finishedPercent = -1;
    return true;
}
