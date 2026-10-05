#include "learn/neckwalkexercise.h"

#include "audio/audio.h"
#include "core/music.h"
#include "core/routine.h"
#include "imgui.h"
#include "input/noteinput.h"
#include "raylib.h"
#include "ui/menulist.h"
#include "ui/neckcards.h"
#include "ui/scoreboard.h"
#include "ui/theme.h"
#include "ui/ui.h"

#include <algorithm>
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
const float CROWD_S = 1.8f;
const float JUDGED_FLASH_S = 0.6f;
const float VERDICT_SHOWN_S = 1.4f;
const int MIN_BPM = 60, MAX_BPM = 200;

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
    return TextFormat("%s, %s, %d to %d strings", what, rhythm, level.minStrings, level.maxStrings);
}

NeckWalkExercise::NeckWalkExercise(const std::string& title, const std::string& tunePath, bool onBass, int level,
                                   const std::string& progressPath, const Settings& settings)
    : title(title), onBass(onBass), tuning(onBass ? WALK_BASS : WALK_GUITAR), progressPath(progressPath), settings(settings){
    stats = loadNeckWalkStats(progressPath);
    if (!loadGroove(tunePath, groove, grooveError)) groove = Groove{};
    // The last game's level and tempo, or the exercise's and the tune's own
    levelIndex = std::clamp(stats.lastLevel >= 0 ? stats.lastLevel : level, 0, NECK_WALK_LEVELS - 1);
    bpm = std::clamp(stats.lastBpm > 0 ? stats.lastBpm : (int)std::lround(groove.tempo), MIN_BPM, MAX_BPM);
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
    startNeckWalk(game, levelIndex, tuning, seed, audioTime() + START_DELAY_S, (float)bpm);
    scheduledTo = audioTime();
    crowdRound = -1;
    judgedAt.assign(game.now.walk.size(), -100.0);
    verdictAt = overAt = -100.0;
    roundAt = GetTime();
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
}

// Every sound whose time comes within the lookahead, each on its own: the tune's hits, the computer's notes. (A whole
// bar at once took every voice there was, cutting off what still rang at the end of the bar before.)
void NeckWalkExercise::scheduleTune(){
    const double from = scheduledTo, horizon = audioTime() + LOOKAHEAD_S;
    if (horizon <= from) return;
    const double beat = game.beatSeconds, barSeconds = NECK_WALK_BAR_BEATS * beat;
    const int phraseBars = std::max(1, (int)groove.phrase.size());
    // The tune: the bar before the first round is the phrase's last (a lead-in to it), then the phrase over and over
    // from each round's first bar, in its key
    for (int bar = std::max(0, (int)std::floor((from - game.startTime) / barSeconds)); ; bar++){
        const double barStart = neckWalkBarTime(game, bar);
        if (barStart >= horizon) break;
        const bool intro = bar < game.now.firstBar && game.round == 0;
        const NeckWalkRound& round = bar >= game.next.firstBar ? game.next : game.now;
        if (game.over && &round == &game.next) break; // it ends with the round it's in
        const int inPhrase = intro ? phraseBars - 1 : (bar - round.firstBar) % phraseBars;
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
            playStringNoteAt(midiToFrequency((float)round->walk[i].pitch), onBass, (float)(length * beat) + RING_S, at, COMPUTER_VOLUME);
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
    if (events.newRound){
        roundAt = now;
        judgedAt.assign(game.now.walk.size(), -100.0);
    }
    if (events.over) finish();
}

void NeckWalkExercise::played(int pitch, double time){
    if (state == State::Playing) handle(neckWalkPlayed(game, pitch, time));
}

void NeckWalkExercise::update(){
    // The notes heard, placed on the tune's clock (the device's own delay taken off)
    if (listening){
        for (const PlayedNote& note : updateNoteInput())
            played(note.pitch, gameTime() - note.age - settings.inputOffsetMs / 1000.0);
    }
    if (state != State::Playing){
        // The level and the tempo, before a game
        const int step = ImGui::GetIO().KeyShift ? 1 : 5;
        if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow)) levelIndex = std::max(0, levelIndex - 1);
        if (ImGui::IsKeyPressed(ImGuiKey_RightArrow)) levelIndex = std::min(NECK_WALK_LEVELS - 1, levelIndex + 1);
        if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) bpm = std::min(MAX_BPM, bpm + step);
        if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) bpm = std::max(MIN_BPM, bpm - step);
        if (ImGui::IsKeyPressed(ImGuiKey_Space, false) && grooveError.empty()) start();
    }
    if (state == State::Playing) handle(neckWalkUpdate(game, gameTime()));
    if (state != State::Ready) scheduleTune(); // over, the round it's in still plays out
}

// The level and the tempo, as buttons (a click goes to the next choice; the arrows do the same)
void NeckWalkExercise::drawChoices(float left, float top, float s){
    const UiFonts& fonts = uiFonts();
    struct Choice { std::string text; const char* key; };
    const Choice choices[2] = {
        { neckWalkLevel(levelIndex).name, "Left Right" },
        { TextFormat("%d bpm", bpm), "Up Down" },
    };
    float x = left;
    for (int i = 0; i < 2; i++){
        if (menuPill(choices[i].text.c_str(), choices[i].key, ImVec2(x, top), false, 0, s)){
            if (i == 0) levelIndex = (levelIndex + 1) % NECK_WALK_LEVELS;
            else bpm = bpm + 5 > MAX_BPM ? MIN_BPM : bpm + 5;
        }
        const float textWidth = fonts.bold->CalcTextSizeA(16 * s, FLT_MAX, 0.0f, choices[i].text.c_str()).x;
        const float keyWidth = fonts.mono->CalcTextSizeA(12 * s, FLT_MAX, 0.0f, choices[i].key).x;
        x += 12 * s + textWidth + 12 * s + keyWidth + 12 * s + 10 * s;
    }
    ImGui::GetWindowDrawList()->AddText(fonts.text, 16 * s, ImVec2(x + 8 * s, top + 7 * s), uiColor(UiColor::Dim),
                                        levelText(neckWalkLevel(levelIndex)).c_str());
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
    const double along = (gameTime() - neckWalkRoundStart(game, round)) / game.beatSeconds;
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
    const int frets = std::max(12, (state == State::Playing ? game.level : neckWalkLevel(levelIndex)).maxFret);
    board = fretboardLayout(left, top, right - left, s, strings, 0, frets, spacing);
    drawFretboard(board, tuning);
    const NeckWalkRound& round = game.now;
    if (state != State::Playing || round.walk.empty()) return;
    float halfW, halfH;
    cardSize(board, halfW, halfH);
    const double along = (gameTime() - neckWalkRoundStart(game, round)) / game.beatSeconds;
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
            const float since = (float)((along - round.onsets[i]) * game.beatSeconds);
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
    draw->AddText(fonts.mono, 14 * s, ImVec2(left, top), uiColor(UiColor::Dim), TextFormat("GAME OVER  ·  %s, %d BPM", game.level.name, (int)std::lround(game.tempo)));
    draw->AddText(fonts.heavy, 56 * s * (0.8f + 0.2f * grow), ImVec2(left, top + 20 * s), uiColor(newBest ? UiColor::Accent : UiColor::Ink),
                  TextFormat("%lld", game.score));
    std::string line = TextFormat("%d %s cleared  ·  best streak %d", game.cleared, game.cleared == 1 ? "round" : "rounds", game.bestStreak);
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
    draw->AddText(fonts.bold, 20 * s, ImVec2(left, y + 76 * s), uiColor(UiColor::Ink), "Space to play again.");
}

void NeckWalkExercise::draw(){
    menuTitle(title.c_str());
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const UiFonts& fonts = uiFonts();
    const float s = menuScale(), width = ImGui::GetWindowWidth(), height = ImGui::GetWindowHeight();
    const float left = width * 0.07f, right = width * 0.93f;
    const bool playing = state != State::Ready;
    const int shownLevel = state == State::Playing ? game.levelIndex : levelIndex;
    const long long best = neckWalkBest(stats, shownLevel);
    drawScoreboard({
        { "SCORE", playing ? std::string(TextFormat("%lld", game.score)) : std::string("-"), UiColor::Accent, "" },
        { "STREAK", playing ? std::string(TextFormat("%d", game.streak)) : std::string("-"), UiColor::Good, "rounds" },
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
                      TextFormat("ROUND %d  ·  %s, %d BPM", game.round + 1, game.level.name, (int)std::lround(game.tempo)));
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
        draw->AddText(fonts.bold, 20 * s, ImVec2(left, textY), uiColor(UiColor::Ink), "Space to start.");
        draw->AddText(fonts.text, 16 * s, ImVec2(left, textY + 28 * s), uiColor(UiColor::Dim),
                      "Each round, a note. The computer walks it on the neck, from the top string down and back up: watch and listen.");
        draw->AddText(fonts.text, 16 * s, ImVec2(left, textY + 50 * s), uiColor(UiColor::Dim),
                      "Then play it back, at the same pace, from memory. All right: cheers. Half right: claps. Less: a life gone, five and it's over.");
        textY += 80 * s;
    }
    if (!grooveError.empty()) draw->AddText(fonts.text, 16 * s, ImVec2(left, textY), uiColor(UiColor::Bad), ("The tune: " + grooveError).c_str());
    else if (!inputError.empty()) draw->AddText(fonts.text, 16 * s, ImVec2(left, textY), uiColor(UiColor::Bad), inputError.c_str());
    else if (!listening) draw->AddText(fonts.text, 16 * s, ImVec2(left, textY), uiColor(UiColor::Dim), "No instrument: click the frets to play");
    menuScreenHint(state == State::Playing ? "Esc  back" : "Left/Right  level    Up/Down  tempo (Shift: by 1)    Space  start    Esc  back", s);
    ImGui::Dummy(ImVec2(1, 1)); // the board moved ImGui's cursor (ui/fretboardview): an item after it
}
