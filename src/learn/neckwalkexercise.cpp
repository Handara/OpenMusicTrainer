#include "learn/neckwalkexercise.h"

#include "audio/audio.h"
#include "core/music.h"
#include "core/routine.h"
#include "imgui.h"
#include "app/playerprogress.h"
#include "input/menuinput.h"
#include "input/noteinput.h"
#include "raylib.h"
#include "ui/menulist.h"
#include "ui/neckcards.h"
#include "ui/scoreboard.h"
#include "ui/theme.h"
#include "ui/ui.h"

#include <algorithm>
#include <filesystem>
#include <cfloat>
#include <cmath>
#include <random>

const std::vector<int> WALK_GUITAR = { 40, 45, 50, 55, 59, 64 };
const std::vector<int> WALK_BASS = { 28, 33, 38, 43 };
const double START_DELAY_S = 0.6;  // from Space to the tune's first beat
const double LOOKAHEAD_S = 0.3;    // each sound is scheduled this far ahead, on the audio clock: to the sample
// The levels: the computer's walk loud, the riff under it (louder in the bar before the first round)
const float COMPUTER_VOLUME = 0.95f, INTRO_GUITAR_VOLUME = 0.55f, RIFF_GUITAR_VOLUME = 0.3f, BASS_VOLUME = 0.7f,
            DRUM_VOLUME = 0.55f, CROWD_VOLUME = 0.8f;
const float RING_S = 0.25f;        // the riff's notes ring a little past their length, as a guitar's do
const float CROWD_S = 1.4f;         // short: the next call comes right after it
const float MORE_STRINGS_SHOWN_S = 2.5f;
const float JUDGED_FLASH_S = 0.6f;
const float VERDICT_SHOWN_S = 1.4f;
const int FIRST_BPM = 100;         // a first game's tempo: slow, to start with

static std::string todayText(){
    int year, month, day;
    dateFromDays(today(), year, month, day);
    return TextFormat("%04d-%02d-%02d", year, month, day);
}

// The crowd's verdict, in color: all right green, half right plain, less red
static UiColor verdictColor(NeckWalkVerdict verdict){
    return verdict == NeckWalkVerdict::Cheer ? UiColor::Good : verdict == NeckWalkVerdict::Claps ? UiColor::Ink : UiColor::Bad;
}

// What each level asks, said plainly
static std::string levelText(const NeckWalkLevel& level){
    const char* what = level.sequence == NeckWalkSequence::Straight ? "the note down the strings and back up"
                     : level.sequence == NeckWalkSequence::Patterns ? "the note in patterns: skipping strings, outside in, at random"
                     : "the triad, up and down in one place";
    const char* rhythm = level.rhythm == NeckWalkRhythm::Even ? "a note a beat" : level.rhythm == NeckWalkRhythm::Mixed ? "a mixed rhythm" : "syncopated";
    return TextFormat("%s, %s", what, rhythm);
}

NeckWalkExercise::NeckWalkExercise(const std::string& title, const std::string& tunePath, bool onBass, int level,
                                   const std::string& progressPath, const Settings& settings)
    : title(title), onBass(onBass), tuning(onBass ? WALK_BASS : WALK_GUITAR), progressPath(progressPath), settings(settings){
    stats = loadNeckWalkStats(progressPath);
    if (!loadGroove(tunePath, groove, grooveError)) groove = Groove{};
    // The last game's setup, or the exercise's level, slowly, on two strings
    if (stats.chosen) setup = stats.choice;
    else {
        setup.level = level;
        setup.bpm = FIRST_BPM;
    }
    setup.level = std::clamp(setup.level, 0, NECK_WALK_LEVELS - 1);
    setup.bpm = std::clamp(setup.bpm, NECK_WALK_MIN_BPM, NECK_WALK_MAX_BPM);
    setup.strings = std::clamp(setup.strings, NECK_WALK_MIN_STRINGS, (int)tuning.size());
    // The kit and the crowd, made once, at the engine's own rate
    const int rate = std::max(1, audioSampleRate());
    const float kitSeconds[5] = { 0.5f, 0.35f, 0.12f, 0.5f, 2.0f };
    for (int drum = 0; drum < 5; drum++){
        kit[drum].resize((size_t)(kitSeconds[drum] * rate));
        renderKitDrum(kit[drum].data(), (int)kit[drum].size(), rate, (KitDrum)drum, 1);
    }
    const CrowdReaction reactions[3] = { CrowdReaction::Cheer, CrowdReaction::Claps, CrowdReaction::Aww }; // as NeckWalkVerdict
    for (int i = 0; i < 3; i++){
        crowd[i].resize((size_t)(CROWD_S * rate));
        renderCrowd(crowd[i].data(), (int)crowd[i].size(), rate, reactions[i], 1);
    }
    const InputRole role = onBass ? InputRole::Bass : InputRole::Guitar;
    listening = startNoteInput(settings.inputDevice, midiToFrequency((float)tuning.front()) * 0.9f, inputError, channelFor(settings, role));
    ImGui::GetIO().ConfigFlags &= ~ImGuiConfigFlags_NavEnableKeyboard; // the arrows and Space choose and play here
}

NeckWalkExercise::~NeckWalkExercise(){
    stopPreviews(); // the tune scheduled ahead, the crowd
    stopNoteInput();
    ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
}

double NeckWalkExercise::gameTime() const {
    return audioTime() - settings.globalOffsetMs / 1000.0;
}

void NeckWalkExercise::start(){
    const unsigned seed = std::random_device{}() ^ (unsigned)(GetTime() * 1000.0);
    startNeckWalk(game, setup, tuning, seed, audioTime() + START_DELAY_S);
    scheduledTo = audioTime();
    crowdRound = -1;
    judgedAt.assign(game.now.walk.size(), -100.0);
    verdictAt = overAt = moreStringsAt = fasterAt = -100.0;
    roundAt = GetTime();
    lastRoot = game.now.root;
    newBest = false;
    state = State::Playing;
}

void NeckWalkExercise::finish(){
    state = State::Over;
    overAt = GetTime();
    bestCleared = std::max(bestCleared, game.cleared);
    newBest = game.score > neckWalkBest(stats, game.levelIndex) && game.score > 0;
    addNeckWalkGame(stats, game, todayText());
    std::string error;
    if (!saveNeckWalkStats(progressPath, stats, error)) TraceLog(LOG_WARNING, "Progress: %s", error.c_str());
    Activity activity; // in the player's journal: XP for the rounds cleared
    activity.kind = ActivityKind::Game;
    activity.id = std::filesystem::path(progressPath).stem().string();
    activity.title = title;
    activity.instrument = onBass ? "bass" : "guitar";
    activity.seconds = (float)std::max(0.0, audioTime() - game.startTime);
    activity.rounds = game.cleared;
    activity.tempo = (int)game.fastest;
    recordActivity(activity);
}

// Every sound whose time comes within the lookahead, each on its own: the tune's hits, the computer's notes. (A whole
// bar at once took every voice there was, cutting off what still rang at the end of the bar before.)
void NeckWalkExercise::scheduleTune(){
    const double from = scheduledTo, horizon = audioTime() + LOOKAHEAD_S;
    if (horizon <= from) return;
    const int phraseBars = std::max(1, (int)groove.phrase.size());
    // The tune: the bar before the first round is the phrase's last (a lead-in to it), then the phrase over and over,
    // going on through the rounds whatever their length, in each one's key; a crash where the key changes
    for (int bar = std::max(0, neckWalkBarAt(game, from)); ; bar++){
        const double barStart = neckWalkBarTime(game, bar);
        if (barStart >= horizon) break;
        const bool intro = bar < NECK_WALK_INTRO_BARS;
        const NeckWalkRound& round = bar >= game.next.firstBar ? game.next : game.now; // its key and its tempo
        const double beat = round.beatSeconds;
        if (game.over && &round == &game.next) break; // it ends with the round it's in
        const int inPhrase = intro ? phraseBars - 1 : (bar - NECK_WALK_INTRO_BARS) % phraseBars;
        const bool newKey = bar == game.next.firstBar && game.next.root != game.now.root;
        if (newKey && inPhrase != 0 && barStart >= from) playSamplesAt(kit[(int)KitDrum::Crash], barStart, DRUM_VOLUME); // (the phrase's first bar has its own)
        for (const GrooveHit& hit : grooveBar(groove, inPhrase, round.root)){
            const double at = barStart + hit.beat * beat;
            if (at < from || at >= horizon) continue;
            const float seconds = (float)(hit.length * beat);
            switch (hit.part){
                case GroovePart::Guitar:
                    playStringNoteAt(midiToFrequency((float)hit.pitch), false, seconds + RING_S, at, intro ? INTRO_GUITAR_VOLUME : RIFF_GUITAR_VOLUME);
                    break;
                case GroovePart::Bass:
                    playStringNoteAt(midiToFrequency((float)hit.pitch), true, seconds, at, BASS_VOLUME);
                    break;
                case GroovePart::Drums:
                    playSamplesAt(kit[(int)hit.drum], at, DRUM_VOLUME);
                    break;
            }
        }
    }
    // The computer's notes, on the instrument played: this round's, and the next's (known a round ahead), each ringing
    // until the next
    for (const NeckWalkRound* round : { &game.now, &game.next }){
        if (game.over && round == &game.next) break;
        for (int i = 0; i < (int)round->walk.size(); i++){
            const double at = neckWalkShowTime(game, *round, i);
            if (at < from || at >= horizon) continue;
            const double length = i + 1 < (int)round->onsets.size() ? round->onsets[i + 1] - round->onsets[i] : 1.0;
            playStringNoteAt(midiToFrequency((float)round->walk[i].pitch), onBass, (float)(length * round->beatSeconds) + RING_S, at, COMPUTER_VOLUME);
        }
    }
    scheduledTo = horizon;
    // The crowd, on the verdict's beat, as soon as the player's notes are all judged
    if (crowdRound != game.round && !game.judged && !game.over && !game.now.walk.empty()
        && std::none_of(game.notes.begin(), game.notes.end(), [](WalkNote note){ return note == WalkNote::Due; })){
        playSamplesAt(crowd[(int)neckWalkVerdictOf(game)], neckWalkVerdictTime(game), CROWD_VOLUME);
        crowdRound = game.round;
    }
}

void NeckWalkExercise::handle(const NeckWalkEvents& events){
    const double now = GetTime();
    for (int index : { events.right, events.wrong, events.missed })
        if (index >= 0 && index < (int)judgedAt.size()) judgedAt[index] = now;
    if (events.right >= 0) playHitSound(true);
    if (events.verdict){
        verdictAt = now;
        lastVerdict = events.how;
    }
    if (events.moreStrings) moreStringsAt = now;
    if (events.faster) fasterAt = now;
    if (events.newRound){
        if (game.now.root != lastRoot) roundAt = now; // a new key: the name pops
        lastRoot = game.now.root;
        judgedAt.assign(game.now.walk.size(), -100.0);
    }
    if (events.over) finish();
}

void NeckWalkExercise::played(int pitch, double time){
    if (state == State::Playing) handle(neckWalkPlayed(game, pitch, time));
}

void NeckWalkExercise::update(){
    // The notes heard, placed on the tune's clock (the device's own delay taken off)
    if (listening && (state == State::Playing || !menuInputActive())){ // between games, the menus read the instrument
        for (const PlayedNote& note : updateNoteInput())
            played(note.pitch, gameTime() - note.age - settings.inputOffsetMs / 1000.0);
    }
    if (state != State::Playing){
        // The level and the tempo, before a game
        const int step = ImGui::GetIO().KeyShift ? 1 : 5;
        const bool back = ImGui::GetIO().KeyShift;
        if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow)) setup.level = std::max(0, setup.level - 1);
        if (ImGui::IsKeyPressed(ImGuiKey_RightArrow)) setup.level = std::min(NECK_WALK_LEVELS - 1, setup.level + 1);
        if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) setup.bpm = std::min(NECK_WALK_MAX_BPM, setup.bpm + step);
        if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) setup.bpm = std::max(NECK_WALK_MIN_BPM, setup.bpm - step);
        if (ImGui::IsKeyPressed(ImGuiKey_S)) cycleChoice(2, back);
        if (ImGui::IsKeyPressed(ImGuiKey_G)) cycleChoice(3, back);
        if (ImGui::IsKeyPressed(ImGuiKey_T)) cycleChoice(4, back);
        const bool go = ImGui::IsKeyPressed(ImGuiKey_Space, false) || ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false);
        if (go && grooveError.empty()) start(); // Enter: the instrument's choose too
    }
    if (state == State::Playing) handle(neckWalkUpdate(game, gameTime()));
    if (state != State::Ready) scheduleTune(); // over, the round it's in still plays out
}

// A choice, one step on (or back), going round: 0 the level, 1 the tempo, 2 the strings to start on, 3 the rounds all
// right before one more string, 4 before faster (0 for those two: never)
void NeckWalkExercise::cycleChoice(int which, bool back){
    auto cycle = [back](int value, int low, int high){ return back ? (value > low ? value - 1 : high) : (value < high ? value + 1 : low); };
    switch (which){
        case 0: setup.level = cycle(setup.level, 0, NECK_WALK_LEVELS - 1); break;
        case 1: setup.bpm = back ? (setup.bpm - 5 < NECK_WALK_MIN_BPM ? NECK_WALK_MAX_BPM : setup.bpm - 5)
                                 : (setup.bpm + 5 > NECK_WALK_MAX_BPM ? NECK_WALK_MIN_BPM : setup.bpm + 5); break;
        case 2: setup.strings = cycle(setup.strings, NECK_WALK_MIN_STRINGS, (int)tuning.size()); break;
        case 3: setup.stringsEvery = cycle(setup.stringsEvery, 0, NECK_WALK_MOST_EVERY); break;
        case 4: setup.tempoEvery = cycle(setup.tempoEvery, 0, NECK_WALK_MOST_EVERY); break;
    }
}

// The setup, as buttons (a click goes to the next choice, its key does the same, Shift with it goes back), in two
// rows: what's played, and how it grows
void NeckWalkExercise::drawChoices(float left, float top, float s){
    const UiFonts& fonts = uiFonts();
    struct Choice { std::string text; const char* key; int which; };
    const std::vector<Choice> rows[2] = {
        { { neckWalkLevel(setup.level).name, "Left Right", 0 }, { TextFormat("%d bpm", setup.bpm), "Up Down", 1 },
          { TextFormat("from %d strings", setup.strings), "S", 2 } },
        { { setup.stringsEvery == 0 ? std::string("strings stay")
                                    : std::string(TextFormat("a string more every %d clean %s", setup.stringsEvery, setup.stringsEvery == 1 ? "run" : "runs")), "G", 3 },
          { setup.tempoEvery == 0 ? std::string("tempo stays")
                                  : std::string(TextFormat("+%d bpm every %d clean %s", NECK_WALK_TEMPO_STEP, setup.tempoEvery, setup.tempoEvery == 1 ? "run" : "runs")), "T", 4 } },
    };
    for (int row = 0; row < 2; row++){
        float x = left;
        const float y = top + row * 40 * s;
        for (const Choice& choice : rows[row]){
            if (menuPill(choice.text.c_str(), choice.key, ImVec2(x, y), false, 0, s)) cycleChoice(choice.which, ImGui::GetIO().KeyShift);
            const float textWidth = fonts.bold->CalcTextSizeA(16 * s, FLT_MAX, 0.0f, choice.text.c_str()).x;
            const float keyWidth = fonts.mono->CalcTextSizeA(12 * s, FLT_MAX, 0.0f, choice.key).x;
            x += 12 * s + textWidth + 12 * s + keyWidth + 12 * s + 10 * s;
        }
        if (row == 0)
            ImGui::GetWindowDrawList()->AddText(fonts.text, 16 * s, ImVec2(x + 8 * s, y + 7 * s), uiColor(UiColor::Dim), levelText(neckWalkLevel(setup.level)).c_str());
    }
}

// The round, along the top: the computer's part (its notes lit as it plays them), the player's (each note as it went),
// the crowd's beat, and the playhead going along; before the first round, the count
void NeckWalkExercise::drawStrip(float left, float right, float top, float s){
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const UiFonts& fonts = uiFonts();
    const NeckWalkRound& round = game.now;
    const int partBeats = round.partBars * NECK_WALK_BAR_BEATS, roundBeats = 2 * partBeats;
    const float height = 46 * s, pad = 16 * s;
    const float beatWidth = (right - left - 2 * pad) / roundBeats;
    auto beatX = [&](double beat){ return left + pad + (float)beat * beatWidth; };
    const double along = (gameTime() - neckWalkRoundStart(game, round)) / round.beatSeconds;
    const double split = partBeats - 0.5;
    const bool watching = along >= 0.0 && along < split, playing = along >= split;
    // The two parts, each a card; the one going on lit
    struct Part { float x0, x1; const char* label; bool on; };
    const Part parts[2] = { { left, beatX(split) - 3 * s, "WATCH", watching }, { beatX(split) + 3 * s, right, "YOUR TURN", playing } };
    for (const Part& part : parts){
        draw->AddRectFilled(ImVec2(part.x0, top), ImVec2(part.x1, top + height), part.on ? uiColor(UiColor::Accent, 0.16f) : uiColor(UiColor::Card), 8 * s);
        draw->AddRect(ImVec2(part.x0, top), ImVec2(part.x1, top + height), uiColor(part.on ? UiColor::Accent : UiColor::StaffLine), 8 * s, 0,
                      (part.on ? 2.0f : 1.0f) * s);
        draw->AddText(fonts.mono, 13 * s, ImVec2(part.x0 + 12 * s, top + 6 * s), uiColor(part.on ? UiColor::Accent : UiColor::Dim), part.label);
    }
    for (int b = 1; b < roundBeats; b++){
        if (b == partBeats) continue;
        const bool barLine = b % NECK_WALK_BAR_BEATS == 0;
        draw->AddLine(ImVec2(beatX(b), top + height - (barLine ? 16 : 7) * s), ImVec2(beatX(b), top + height), uiColor(UiColor::Dim, barLine ? 0.8f : 0.5f), 1.5f * s);
    }
    const float radius = std::min(8 * s, beatWidth * 0.22f), dotY = top + height * 0.6f;
    int next = -1;
    for (int i = 0; i < (int)game.notes.size() && next < 0; i++) if (game.notes[i] == WalkNote::Due) next = i;
    for (int i = 0; i < (int)round.walk.size(); i++){
        // The computer's: lit once played
        const double onset = round.onsets[i];
        if (along >= onset) draw->AddCircleFilled(ImVec2(beatX(onset), dotY), radius, uiColor(UiColor::Accent, 0.9f), 24);
        else draw->AddCircle(ImVec2(beatX(onset), dotY), radius, uiColor(UiColor::Ink, 0.4f), 24, 2 * s);
        // The player's: to come (the next lit), right, wrong
        const ImVec2 at(beatX(partBeats + onset), dotY);
        const WalkNote how = game.notes[i];
        if (how == WalkNote::Due){
            const bool lit = i == next && playing;
            draw->AddCircle(at, radius, uiColor(lit ? UiColor::Accent : UiColor::Ink, lit ? 1.0f : 0.4f), 24, 2 * s);
        } else {
            const float since = i < (int)judgedAt.size() ? (float)(GetTime() - judgedAt[i]) : 99.0f;
            const float pop = since < 0.25f ? 4 * s * (1.0f - since / 0.25f) : 0.0f;
            draw->AddCircleFilled(at, radius + pop, uiColor(how == WalkNote::Right ? UiColor::Good : UiColor::Bad), 24);
        }
    }
    // The crowd's beat: a diamond, colored by the verdict once it's in
    const ImVec2 crowdAt(beatX(roundBeats - 0.5), dotY);
    const float r = 8 * s;
    draw->AddQuadFilled(ImVec2(crowdAt.x, crowdAt.y - r), ImVec2(crowdAt.x + r, crowdAt.y), ImVec2(crowdAt.x, crowdAt.y + r), ImVec2(crowdAt.x - r, crowdAt.y),
                        uiColor(!game.judged ? UiColor::Dim : verdictColor(lastVerdict)));
    if (along >= 0.0 && along <= roundBeats){
        const float x = beatX(along);
        draw->AddLine(ImVec2(x, top - 4 * s), ImVec2(x, top + height + 4 * s), uiColor(UiColor::Ink), 3 * s);
    } else if (along < 0.0){
        // Counting in: the beats left, big, on the computer's card
        const char* count = TextFormat("%d", (int)std::ceil(-along));
        draw->AddText(fonts.heavy, 30 * s, ImVec2(left + 110 * s, top + 6 * s), uiColor(UiColor::Accent), count);
    }
}

// Play mode's neck. Watching: the walk's places, each lit as the computer plays it, the way to it from the one before.
// The player's turn: the neck bare, only what was just played showing, green, or red where it should have been.
void NeckWalkExercise::drawNeck(float left, float right, float top, float bottom, float s){
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const UiFonts& fonts = uiFonts();
    const int strings = (int)tuning.size();
    const float spacing = std::min(42.0f, (bottom - top) / s / (float)strings);
    const int frets = std::max(12, (state == State::Playing ? game.level : neckWalkLevel(setup.level)).maxFret);
    board = fretboardLayout(left, top, right - left, s, strings, 0, frets, spacing);
    drawFretboard(board, tuning);
    const NeckWalkRound& round = game.now;
    if (state != State::Playing || round.walk.empty()) return;
    float halfW, halfH;
    cardSize(board, halfW, halfH);
    const double along = (gameTime() - neckWalkRoundStart(game, round)) / round.beatSeconds;
    auto centered = [&](const char* text, UiColor color){
        const ImVec2 size = fonts.bold->CalcTextSizeA(22 * s, FLT_MAX, 0.0f, text);
        const ImVec2 at((left + right) / 2 - size.x / 2, board.top + board.height / 2 - size.y / 2);
        draw->AddRectFilled(ImVec2(at.x - 16 * s, at.y - 8 * s), ImVec2(at.x + size.x + 16 * s, at.y + size.y + 8 * s), uiColor(UiColor::Background, 0.85f), 10 * s);
        draw->AddText(fonts.bold, 22 * s, at, uiColor(color), text);
    };
    if (along < 0.0){
        centered("Get ready: watch, then play it back", UiColor::Dim);
        return;
    }
    if (along < round.partBars * NECK_WALK_BAR_BEATS - 0.5){
        // The computer's turn
        int playedUpTo = -1;
        for (int i = 0; i < (int)round.walk.size(); i++) if (along >= round.onsets[i]) playedUpTo = i;
        for (int i = 0; i < (int)round.walk.size(); i++){
            const NeckStep& step = round.walk[i];
            bool later = false; // the same place again, played since: that one shows
            for (int j = i + 1; j <= playedUpTo; j++) later = later || (round.walk[j].string == step.string && round.walk[j].fret == step.fret);
            if (later) continue;
            const bool isNow = i == playedUpTo;
            const float since = (float)((along - round.onsets[i]) * round.beatSeconds);
            const float pop = isNow ? 5 * s * std::exp(-since * 8.0f) : 0.0f;
            drawNoteCard(draw, board, step.string, step.fret, step.pitch, pop, i <= playedUpTo ? (isNow ? 1.0f : 0.6f) : 0.25f, isNow, s);
        }
        if (playedUpTo > 0){
            const NeckStep& from = round.walk[playedUpTo - 1];
            const NeckStep& to = round.walk[playedUpTo];
            drawWay(draw, board, ImVec2(board.fretX(from.fret), board.stringY(from.string)), ImVec2(board.fretX(to.fret), board.stringY(to.string)), 1.0f, 0.8f, s);
        }
        return;
    }
    // The player's turn: nothing shown but what was just played
    bool shownAny = false;
    for (int i = 0; i < (int)judgedAt.size() && i < (int)round.walk.size(); i++){
        const float since = (float)(GetTime() - judgedAt[i]);
        if (since > JUDGED_FLASH_S || game.notes[i] == WalkNote::Due) continue;
        shownAny = true;
        const NeckStep& step = round.walk[i];
        const bool wasRight = game.notes[i] == WalkNote::Right;
        const float fade = 1.0f - since / JUDGED_FLASH_S;
        if (wasRight) drawNoteCard(draw, board, step.string, step.fret, step.pitch, 0.0f, fade, false, s);
        cardOutline(draw, ImVec2(board.fretX(step.fret), board.stringY(step.string)), halfW, halfH, 3 * s + 12 * s * since / JUDGED_FLASH_S,
                    uiColor(wasRight ? UiColor::Good : UiColor::Bad, fade), 2.5f * s);
    }
    if (!shownAny && !game.judged && std::any_of(game.notes.begin(), game.notes.end(), [](WalkNote note){ return note == WalkNote::Due; }))
        centered("Your turn", UiColor::Accent);
}

// After the game: the score, the rounds, a new best, and how each note went
void NeckWalkExercise::drawResults(float left, float top, float width, float s){
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const UiFonts& fonts = uiFonts();
    const float since = (float)(GetTime() - overAt);
    const float grow = std::min(1.0f, since / 0.3f);
    draw->AddText(fonts.mono, 14 * s, ImVec2(left, top), uiColor(UiColor::Dim), TextFormat("GAME OVER  ·  %s, from %d BPM", game.level.name, game.setup.bpm));
    draw->AddText(fonts.heavy, 56 * s * (0.8f + 0.2f * grow), ImVec2(left, top + 20 * s), uiColor(newBest ? UiColor::Accent : UiColor::Ink),
                  TextFormat("%lld", game.score));
    std::string line = TextFormat("%d %s cleared  ·  best streak %d  ·  up to %d strings, %d bpm", game.cleared, game.cleared == 1 ? "round" : "rounds",
                                  game.bestStreak, game.mostStrings, (int)std::lround(game.fastest));
    if (newBest) line += "  ·  NEW BEST";
    draw->AddText(fonts.bold, 20 * s, ImVec2(left, top + 90 * s), uiColor(newBest ? UiColor::Accent : UiColor::Ink), line.c_str());
    // Each note this game: how many of its walk notes were right
    float x = left;
    const float y = top + 132 * s, cell = std::min(64 * s, width / 12.0f);
    for (int note = 0; note < 12; note++){
        const int right = game.rightByNote[note], wrong = game.wrongByNote[note];
        if (right + wrong == 0) continue;
        const float share = (float)right / (float)(right + wrong);
        const UiColor color = share >= 0.99f ? UiColor::Good : share >= 0.6f ? UiColor::Ink : UiColor::Bad;
        draw->AddRectFilled(ImVec2(x, y), ImVec2(x + cell - 6 * s, y + 52 * s), uiColor(UiColor::Card), 8 * s);
        draw->AddText(fonts.bold, 20 * s, ImVec2(x + 10 * s, y + 6 * s), uiColor(color), pitchClassName(note));
        draw->AddText(fonts.mono, 12 * s, ImVec2(x + 10 * s, y + 32 * s), uiColor(UiColor::Dim), TextFormat("%d/%d", right, right + wrong));
        x += cell;
    }
    draw->AddText(fonts.bold, 20 * s, ImVec2(left, y + 76 * s), uiColor(UiColor::Ink), menuInputActive() ? "Space, or the open G string, to play again." : "Space to play again.");
}

void NeckWalkExercise::draw(){
    menuTitle(title.c_str());
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const UiFonts& fonts = uiFonts();
    const float s = menuScale(), width = ImGui::GetWindowWidth(), height = ImGui::GetWindowHeight();
    const float left = width * 0.07f, right = width * 0.93f;
    const bool playing = state != State::Ready;
    const int shownLevel = state == State::Playing ? game.levelIndex : setup.level;
    const long long best = neckWalkBest(stats, shownLevel);
    drawScoreboard({
        { "SCORE", playing ? std::string(TextFormat("%lld", game.score)) : std::string("-"), UiColor::Accent, "" },
        { "STREAK", playing ? std::string(TextFormat("%d", game.streak)) : std::string("-"), UiColor::Good, "rounds" },
        { "STRINGS", std::string(TextFormat("%d", playing ? game.now.strings : setup.strings)), UiColor::Ink,
          playing && game.setup.stringsEvery > 0 && game.strings < (int)tuning.size()
              ? std::string(TextFormat("%d/%d clean to grow", game.clearsOnStrings, game.setup.stringsEvery))
              : std::string(TextFormat("of %d", (int)tuning.size())) },
        { "TEMPO", std::string(TextFormat("%d", playing ? (int)std::lround(game.now.tempo) : setup.bpm)), UiColor::Ink,
          playing && game.setup.tempoEvery > 0 && game.tempo < NECK_WALK_MAX_BPM
              ? std::string(TextFormat("%d/%d clean to speed up", game.clearsOnTempo, game.setup.tempoEvery))
              : std::string("bpm") },
        { "LIVES", playing ? std::string(TextFormat("%d", game.lives)) : std::string(TextFormat("%d", NECK_WALK_LIVES)),
          playing && game.lives <= 2 ? UiColor::Bad : UiColor::Ink, TextFormat("of %d", NECK_WALK_LIVES) },
        { "BEST", best > 0 ? std::string(TextFormat("%lld", best)) : std::string("-"), UiColor::Ink, neckWalkLevel(shownLevel).name },
    }, right, height * 0.03f + 36 * s, s);

    const float stripTop = height * 0.22f;
    if (state == State::Playing) drawStrip(left, right, stripTop, s);
    else drawChoices(left, stripTop + 6 * s, s);

    // The round's note, big, and on which strings
    const float noteTop = stripTop + 58 * s;
    if (state == State::Playing && !game.now.walk.empty()){
        const float since = (float)(GetTime() - roundAt);
        const float pop = 1.0f + 0.25f * std::exp(-since * 6.0f);
        const std::string key = std::string(pitchClassName(game.now.root)) + (game.level.sequence == NeckWalkSequence::Triads ? " triad" : "");
        draw->AddText(fonts.heavy, 54 * s * pop, ImVec2(left, noteTop), uiColor(UiColor::Ink), key.c_str());
        const float keyWidth = fonts.heavy->CalcTextSizeA(54 * s, FLT_MAX, 0.0f, key.c_str()).x;
        std::vector<int> on;
        for (const NeckStep& step : game.now.walk) if (std::find(on.begin(), on.end(), step.string) == on.end()) on.push_back(step.string);
        std::sort(on.begin(), on.end());
        std::string where = "on the ";
        for (size_t i = 0; i < on.size(); i++){
            where += pitchClassName(tuning[on[i]] % 12);
            where += i + 2 < on.size() ? ", " : i + 1 < on.size() ? " and " : " strings";
        }
        draw->AddText(fonts.bold, 18 * s, ImVec2(left + keyWidth + 26 * s, noteTop + 12 * s), uiColor(UiColor::Dim), where.c_str());
        draw->AddText(fonts.mono, 13 * s, ImVec2(left + keyWidth + 26 * s, noteTop + 38 * s), uiColor(UiColor::Dim),
                      TextFormat("ROUND %d  ·  %s, %d BPM", game.round + 1, game.level.name, (int)std::lround(game.now.tempo)));
    }

    // The verdict, big: the crowd's
    const float verdictSince = (float)(GetTime() - verdictAt);
    if (state != State::Ready && verdictSince < VERDICT_SHOWN_S){
        const char* text = lastVerdict == NeckWalkVerdict::Cheer ? "YEAH!" : lastVerdict == NeckWalkVerdict::Claps ? "OK" : "AWWW";
        const float grow = 1.0f + 0.3f * std::exp(-verdictSince * 8.0f), alpha = std::min(1.0f, (VERDICT_SHOWN_S - verdictSince) * 3.0f);
        const float size = 64 * s * grow;
        const ImVec2 measured = fonts.heavy->CalcTextSizeA(size, FLT_MAX, 0.0f, text);
        draw->AddText(fonts.heavy, size, ImVec2(right - measured.x, noteTop - 4 * s), uiColor(verdictColor(lastVerdict), alpha), text);
    }

    // One more string, from the round after next
    // One more string, or faster, from the round after next
    const float moreSince = (float)(GetTime() - std::max(moreStringsAt, fasterAt));
    if (state == State::Playing && moreSince < MORE_STRINGS_SHOWN_S){
        std::string text = "NEXT:";
        if (GetTime() - moreStringsAt < MORE_STRINGS_SHOWN_S) text += TextFormat(" %d STRINGS", game.strings);
        if (GetTime() - fasterAt < MORE_STRINGS_SHOWN_S) text += TextFormat(" %d BPM", (int)std::lround(game.tempo));
        const float alpha = std::min(1.0f, (MORE_STRINGS_SHOWN_S - moreSince) * 3.0f), grow = 1.0f + 0.25f * std::exp(-moreSince * 7.0f);
        const ImVec2 measured = fonts.heavy->CalcTextSizeA(24 * s * grow, FLT_MAX, 0.0f, text.c_str());
        draw->AddText(fonts.heavy, 24 * s * grow, ImVec2(right - measured.x, noteTop + 62 * s), uiColor(UiColor::Accent, alpha), text.c_str());
    }

    const float neckTop = height * 0.43f, neckBottom = height * 0.43f + std::min(42.0f * s * (float)tuning.size(), height * 0.34f);
    if (state == State::Over){
        drawResults(left, height * 0.36f, right - left, s);
    } else {
        drawNeck(left, right, neckTop, neckBottom, s);
        // A click on a fret plays it: for trying it out without an instrument
        const ImVec2 mouse = ImGui::GetMousePos();
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && mouse.y >= board.top && mouse.y <= board.top + board.height){
            const int fret = board.fretAt(mouse.x), string = board.stringAt(mouse.y);
            if (fret >= 0 && string >= 0 && string < (int)tuning.size()){
                playStringNote(midiToFrequency((float)(tuning[string] + fret)), onBass, 1.0f, 0.8f);
                played(tuning[string] + fret, gameTime());
            }
        }
    }

    float textY = neckBottom + 40 * s;
    if (state == State::Ready){
        draw->AddText(fonts.bold, 20 * s, ImVec2(left, textY), uiColor(UiColor::Ink), menuInputActive() ? "Space, or the open G string, to start." : "Space to start.");
        draw->AddText(fonts.text, 16 * s, ImVec2(left, textY + 28 * s), uiColor(UiColor::Dim),
                      "The computer plays a few notes on the neck: watch and listen. Then play them back, from memory, in the same rhythm.");
        draw->AddText(fonts.text, 16 * s, ImVec2(left, textY + 50 * s), uiColor(UiColor::Dim),
                      "Get them right and more strings come in, faster, as you've set. Half right: claps. Less: a life gone, five and it's over.");
        textY += 80 * s;
    }
    if (!grooveError.empty()) draw->AddText(fonts.text, 16 * s, ImVec2(left, textY), uiColor(UiColor::Bad), ("The tune: " + grooveError).c_str());
    else if (!inputError.empty()) draw->AddText(fonts.text, 16 * s, ImVec2(left, textY), uiColor(UiColor::Bad), inputError.c_str());
    else if (!listening) draw->AddText(fonts.text, 16 * s, ImVec2(left, textY), uiColor(UiColor::Dim), "No instrument: click the frets to play");
    menuScreenHint(state == State::Playing ? "Esc  back" : "Left/Right  level    Up/Down  tempo    S  strings    G  grow strings    T  speed up    (Shift: back)    Space  start    Esc  back", s);
    ImGui::Dummy(ImVec2(1, 1)); // the board moved ImGui's cursor (ui/fretboardview): an item after it
}
