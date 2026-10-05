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
const double START_DELAY_S = 0.6;  // from Space to the first beat
const double LOOKAHEAD_S = 0.3;    // the tune is scheduled this far ahead, on the audio clock: to the sample
// The tune's levels: the guitar riff is the call, quieter under the player's walk
const float CALL_GUITAR_VOLUME = 0.6f, WALK_GUITAR_VOLUME = 0.3f, BASS_VOLUME = 0.75f, DRUM_VOLUME = 0.55f, CROWD_VOLUME = 0.9f;
const float CROWD_S = 2.4f;
const float JUDGED_FLASH_S = 0.5f;
const float VERDICT_SHOWN_S = 1.6f;
const int SHOWN_FRETS = 12;

static std::string todayText(){
    int year, month, day;
    dateFromDays(today(), year, month, day);
    return TextFormat("%04d-%02d-%02d", year, month, day);
}

NeckWalkExercise::NeckWalkExercise(const std::string& title, const std::string& tunePath, bool onBass, const std::string& progressPath,
                                   const Settings& settings)
    : title(title), onBass(onBass), tuning(onBass ? WALK_BASS : WALK_GUITAR), progressPath(progressPath), settings(settings){
    stats = loadNeckWalkStats(progressPath);
    if (!loadGroove(tunePath, groove, grooveError)) groove = Groove{};
    // The kit and the crowd, made once, at the engine's own rate
    const int rate = std::max(1, audioSampleRate());
    const float kitSeconds[5] = { 0.5f, 0.35f, 0.12f, 0.5f, 2.0f };
    for (int drum = 0; drum < 5; drum++){
        kit[drum].resize((size_t)(kitSeconds[drum] * rate));
        renderKitDrum(kit[drum].data(), (int)kit[drum].size(), rate, (KitDrum)drum, 1);
    }
    cheer.resize((size_t)(CROWD_S * rate));
    renderCrowd(cheer.data(), (int)cheer.size(), rate, true, 1);
    aww.resize((size_t)(CROWD_S * rate));
    renderCrowd(aww.data(), (int)aww.size(), rate, false, 1);
    const InputRole role = onBass ? InputRole::Bass : InputRole::Guitar;
    listening = startNoteInput(settings.inputDevice, midiToFrequency((float)tuning.front()) * 0.9f, inputError, channelFor(settings, role));
    ImGui::GetIO().ConfigFlags &= ~ImGuiConfigFlags_NavEnableKeyboard; // Space plays here
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
    startNeckWalk(game, neckWalkEasy(), tuning, seed, audioTime() + START_DELAY_S, groove.tempo);
    nextBar = 0;
    crowdRound = -1;
    judgedAt.assign(game.walk.size(), -100.0);
    verdictAt = overAt = -100.0;
    roundAt = GetTime();
    newBest = false;
    state = State::Playing;
}

void NeckWalkExercise::finish(){
    state = State::Over;
    overAt = GetTime();
    bestCleared = std::max(bestCleared, game.cleared);
    newBest = game.score > neckWalkBest(stats) && game.score > 0;
    addNeckWalkGame(stats, game, todayText());
    std::string error;
    if (!saveNeckWalkStats(progressPath, stats, error)) TraceLog(LOG_WARNING, "Progress: %s", error.c_str());
}

void NeckWalkExercise::scheduleTune(){
    const double beat = game.beatSeconds;
    while (true){
        const double barStart = game.startTime + (double)nextBar * NECK_WALK_BAR_BEATS * beat;
        if (barStart > audioTime() + LOOKAHEAD_S) break;
        const int round = nextBar / NECK_WALK_ROUND_BARS, inPhrase = nextBar % NECK_WALK_ROUND_BARS;
        if (game.over && round > game.round) break; // it ends with the phrase it's in
        const int root = ((game.root + 5 * (round - game.round)) % 12 + 12) % 12; // a fourth up each round
        for (const GrooveHit& hit : grooveBar(groove, inPhrase, root)){
            const double at = barStart + hit.beat * beat;
            const float seconds = (float)(hit.length * beat);
            switch (hit.part){
                case GroovePart::Guitar:
                    playStringNoteAt(midiToFrequency((float)hit.pitch), false, seconds, at, inPhrase == 0 ? CALL_GUITAR_VOLUME : WALK_GUITAR_VOLUME);
                    break;
                case GroovePart::Bass:
                    playStringNoteAt(midiToFrequency((float)hit.pitch), true, seconds, at, BASS_VOLUME);
                    break;
                case GroovePart::Drums:
                    playSamplesAt(kit[(int)hit.drum], at, DRUM_VOLUME);
                    break;
            }
        }
        nextBar++;
    }
    // The crowd, on the verdict's beat, as soon as the whole walk is judged
    if (crowdRound != game.round && !game.judged && !game.walk.empty()
        && std::none_of(game.notes.begin(), game.notes.end(), [](WalkNote note){ return note == WalkNote::Due; })){
        const bool clean = std::all_of(game.notes.begin(), game.notes.end(), [](WalkNote note){ return note == WalkNote::Right; });
        playSamplesAt(clean ? cheer : aww, neckWalkVerdictTime(game), CROWD_VOLUME);
        crowdRound = game.round;
    }
}

void NeckWalkExercise::handle(const NeckWalkEvents& events){
    const double now = GetTime();
    for (int index : { events.right, events.wrong, events.missed })
        if (index >= 0 && index < (int)judgedAt.size()) judgedAt[index] = now;
    if (events.right >= 0) playHitSound(true);
    if (events.cheer || events.aww){
        verdictAt = now;
        lastCheer = events.cheer;
    }
    if (events.newRound){
        roundAt = now;
        judgedAt.assign(game.walk.size(), -100.0);
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
    if (ImGui::IsKeyPressed(ImGuiKey_Space, false) && state != State::Playing && grooveError.empty()) start();
    if (state == State::Playing) handle(neckWalkUpdate(game, gameTime()));
    if (state != State::Ready) scheduleTune(); // over, the phrase it's in still plays out
}

// The phrase, along the top: four bars of beats, the first to listen (the call), the walk's notes on theirs (each as
// it went), the crowd's beat, and the playhead going along
void NeckWalkExercise::drawStrip(float left, float right, float top, float s){
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const UiFonts& fonts = uiFonts();
    const float height = 46 * s, width = right - left;
    const float beatWidth = width / NECK_WALK_ROUND_BEATS;
    auto beatX = [&](double beat){ return left + (float)beat * beatWidth; };
    const double roundStart = neckWalkRoundStart(game, game.round);
    const double along = state == State::Playing ? (gameTime() - roundStart) / game.beatSeconds : -1.0;
    // The bars: the call a card of its own, the walk's three bars one card (from half a beat before its first note)
    const float callEnd = beatX(NECK_WALK_BAR_BEATS - 0.5);
    const bool inCall = along >= 0.0 && along < NECK_WALK_BAR_BEATS;
    draw->AddRectFilled(ImVec2(left, top), ImVec2(callEnd - 3 * s, top + height), uiColor(inCall ? UiColor::Accent : UiColor::Card, inCall ? 0.18f : 1.0f), 8 * s);
    draw->AddRect(ImVec2(left, top), ImVec2(callEnd - 3 * s, top + height), uiColor(inCall ? UiColor::Accent : UiColor::StaffLine), 8 * s, 0, (inCall ? 2.0f : 1.0f) * s);
    draw->AddText(fonts.mono, 13 * s, ImVec2(left + 12 * s, top + 8 * s), uiColor(inCall ? UiColor::Accent : UiColor::Dim), "LISTEN");
    draw->AddRectFilled(ImVec2(callEnd + 3 * s, top), ImVec2(right, top + height), uiColor(UiColor::Card), 8 * s);
    draw->AddRect(ImVec2(callEnd + 3 * s, top), ImVec2(right, top + height), uiColor(UiColor::StaffLine), 8 * s, 0, 1.0f * s);
    draw->AddText(fonts.mono, 13 * s, ImVec2(callEnd + 15 * s, top + 8 * s), uiColor(!inCall && along >= 0.0 ? UiColor::Accent : UiColor::Dim), "WALK");
    // Every beat a tick; the bar lines taller
    for (int b = 1; b < NECK_WALK_ROUND_BEATS; b++){
        const bool barLine = b % NECK_WALK_BAR_BEATS == 0;
        draw->AddLine(ImVec2(beatX(b), top + height - (barLine ? 18 : 8) * s), ImVec2(beatX(b), top + height), uiColor(UiColor::Dim, barLine ? 0.8f : 0.5f), 1.5f * s);
    }
    // The walk's notes, each a dot on its beat: hollow to come, green right, red wrong or missed
    const float radius = 9 * s;
    int next = -1;
    for (int i = 0; i < (int)game.notes.size() && next < 0; i++) if (game.notes[i] == WalkNote::Due) next = i;
    for (int i = 0; i < (int)game.walk.size() && state != State::Ready; i++){
        const double beat = NECK_WALK_BAR_BEATS + i * game.level.beatsPerNote;
        const ImVec2 at(beatX(beat), top + height * 0.58f);
        const WalkNote how = game.notes[i];
        const float since = i < (int)judgedAt.size() ? (float)(GetTime() - judgedAt[i]) : 99.0f;
        const float pop = since < 0.25f ? 5 * s * (1.0f - since / 0.25f) : 0.0f;
        if (how == WalkNote::Due){
            const bool lit = i == next && along >= NECK_WALK_BAR_BEATS - game.level.beatsPerNote;
            draw->AddCircle(at, radius, uiColor(lit ? UiColor::Accent : UiColor::Ink, lit ? 1.0f : 0.5f), 24, 2 * s);
        } else {
            draw->AddCircleFilled(at, radius + pop, uiColor(how == WalkNote::Right ? UiColor::Good : UiColor::Bad), 24);
        }
    }
    // The crowd's beat
    if (!game.walk.empty()){
        const double verdictBeat = NECK_WALK_BAR_BEATS + (double)game.walk.size() * game.level.beatsPerNote;
        const ImVec2 at(beatX(verdictBeat), top + height * 0.58f);
        const UiColor color = !game.judged ? UiColor::Dim : lastCheer ? UiColor::Good : UiColor::Bad;
        const float r = 9 * s; // a diamond
        draw->AddQuadFilled(ImVec2(at.x, at.y - r), ImVec2(at.x + r, at.y), ImVec2(at.x, at.y + r), ImVec2(at.x - r, at.y), uiColor(color));
    }
    // The playhead
    if (along >= 0.0 && along <= NECK_WALK_ROUND_BEATS){
        const float x = beatX(along);
        draw->AddLine(ImVec2(x, top - 4 * s), ImVec2(x, top + height + 4 * s), uiColor(UiColor::Ink), 3 * s);
    }
}

// Play mode's neck: the round's places as cards, the next one lit and pulsing on the beat, the way to it, and how
// each went, a ring flashing green or red
void NeckWalkExercise::drawNeck(float left, float right, float top, float bottom, float s){
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const int strings = (int)tuning.size();
    const float spacing = std::min(42.0f, (bottom - top) / s / (float)strings);
    board = fretboardLayout(left, top, right - left, s, strings, 0, SHOWN_FRETS, spacing);
    drawFretboard(board, tuning);
    if (state == State::Ready || game.walk.empty()) return;
    float halfW, halfH;
    cardSize(board, halfW, halfH);
    const double along = (gameTime() - neckWalkRoundStart(game, game.round)) / game.beatSeconds;
    const float beatPhase = (float)(along - std::floor(along));
    const float pulse = along >= 0.0 ? std::exp(-beatPhase * 6.0f) : 0.0f; // on every beat
    int next = -1;
    for (int i = 0; i < (int)game.notes.size(); i++) if (game.notes[i] == WalkNote::Due){ next = i; break; }
    // Each place once, dim; the next one lit
    for (int i = 0; i < (int)game.walk.size(); i++){
        const NeckStep& step = game.walk[i];
        bool seen = false;
        for (int j = 0; j < i; j++) seen = seen || (game.walk[j].string == step.string && game.walk[j].fret == step.fret);
        if (seen) continue;
        const bool isNext = next >= 0 && game.walk[next].string == step.string && game.walk[next].fret == step.fret;
        drawNoteCard(draw, board, step.string, step.fret, step.pitch, isNext ? 3 * s * pulse : 0.0f, isNext ? 1.0f : 0.45f, isNext, s);
    }
    // The way from the one just played to the next, a light going along it to the next one's beat
    if (next > 0){
        const NeckStep& from = game.walk[next - 1];
        const NeckStep& to = game.walk[next];
        const double fromBeat = NECK_WALK_BAR_BEATS + (next - 1) * game.level.beatsPerNote;
        const float light = (float)std::clamp((along - fromBeat) / game.level.beatsPerNote, 0.0, 1.0);
        drawWay(draw, board, ImVec2(board.fretX(from.fret), board.stringY(from.string)), ImVec2(board.fretX(to.fret), board.stringY(to.string)), light, 0.9f, s);
    }
    // How each went: a ring flashing out from its card
    for (int i = 0; i < (int)judgedAt.size() && i < (int)game.walk.size(); i++){
        const float since = (float)(GetTime() - judgedAt[i]);
        if (since > JUDGED_FLASH_S) continue;
        const NeckStep& step = game.walk[i];
        const UiColor color = game.notes[i] == WalkNote::Right ? UiColor::Good : UiColor::Bad;
        cardOutline(draw, ImVec2(board.fretX(step.fret), board.stringY(step.string)), halfW, halfH, 3 * s + 14 * s * since / JUDGED_FLASH_S,
                    uiColor(color, 1.0f - since / JUDGED_FLASH_S), 2.5f * s);
    }
}

// After the game: the score, the rounds, a new best, and how each note went
void NeckWalkExercise::drawResults(float left, float top, float width, float s){
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const UiFonts& fonts = uiFonts();
    const float since = (float)(GetTime() - overAt);
    const float grow = std::min(1.0f, since / 0.3f);
    draw->AddText(fonts.mono, 14 * s, ImVec2(left, top), uiColor(UiColor::Dim), "GAME OVER");
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
    const long long best = neckWalkBest(stats);
    const bool playing = state != State::Ready;
    drawScoreboard({
        { "SCORE", playing ? std::string(TextFormat("%lld", game.score)) : std::string("-"), UiColor::Accent, "" },
        { "STREAK", playing ? std::string(TextFormat("%d", game.streak)) : std::string("-"), UiColor::Good, "rounds" },
        { "LIVES", playing ? std::string(TextFormat("%d", game.lives)) : std::string(TextFormat("%d", neckWalkEasy().lives)),
          playing && game.lives <= 2 ? UiColor::Bad : UiColor::Ink, TextFormat("of %d", neckWalkEasy().lives) },
        { "BEST", best > 0 ? std::string(TextFormat("%lld", best)) : std::string("-"), UiColor::Ink, "" },
    }, right, height * 0.03f + 36 * s, s);

    const float stripTop = height * 0.22f;
    if (state == State::Playing) drawStrip(left, right, stripTop, s);

    // The round's note, big, and where
    const float noteTop = stripTop + 58 * s;
    if (state == State::Playing && !game.walk.empty()){
        const float since = (float)(GetTime() - roundAt);
        const float pop = 1.0f + 0.25f * std::exp(-since * 6.0f);
        draw->AddText(fonts.heavy, 54 * s * pop, ImVec2(left, noteTop), uiColor(UiColor::Ink), pitchClassName(game.root));
        std::vector<int> on;
        for (const NeckStep& step : game.walk) if (std::find(on.begin(), on.end(), step.string) == on.end()) on.push_back(step.string);
        std::sort(on.begin(), on.end());
        std::string where = "on the ";
        for (size_t i = 0; i < on.size(); i++){
            where += pitchClassName(tuning[on[i]] % 12);
            where += i + 2 < on.size() ? ", " : i + 1 < on.size() ? " and " : " strings";
        }
        draw->AddText(fonts.bold, 18 * s, ImVec2(left + 90 * s, noteTop + 12 * s), uiColor(UiColor::Dim), where.c_str());
        draw->AddText(fonts.mono, 13 * s, ImVec2(left + 90 * s, noteTop + 38 * s), uiColor(UiColor::Dim), TextFormat("ROUND %d", game.round + 1));
    }

    // The verdict, big: the crowd's
    const float verdictSince = (float)(GetTime() - verdictAt);
    if (state != State::Ready && verdictSince < VERDICT_SHOWN_S){
        const char* text = lastCheer ? "YEAH!" : "AWWW";
        const float grow = 1.0f + 0.3f * std::exp(-verdictSince * 8.0f), alpha = std::min(1.0f, (VERDICT_SHOWN_S - verdictSince) * 3.0f);
        const float size = 64 * s * grow;
        const ImVec2 measured = fonts.heavy->CalcTextSizeA(size, FLT_MAX, 0.0f, text);
        draw->AddText(fonts.heavy, size, ImVec2(right - measured.x, noteTop - 4 * s), uiColor(lastCheer ? UiColor::Good : UiColor::Bad, alpha), text);
    }

    const float neckTop = height * 0.43f, neckBottom = height * 0.43f + std::min(42.0f * s * (float)tuning.size(), height * 0.34f);
    if (state == State::Over){
        drawResults(left, height * 0.42f, right - left, s);
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
                      "Each round, a note. Listen for a bar, then walk it on the strings shown: from the top one down and back up, a note every two beats.");
        draw->AddText(fonts.text, 16 * s, ImVec2(left, textY + 50 * s), uiColor(UiColor::Dim),
                      "Get the whole walk right and the crowd cheers. Five rounds wrong and it's over.");
        textY += 80 * s;
    }
    if (!grooveError.empty()) draw->AddText(fonts.text, 16 * s, ImVec2(left, textY), uiColor(UiColor::Bad), ("The tune: " + grooveError).c_str());
    else if (!inputError.empty()) draw->AddText(fonts.text, 16 * s, ImVec2(left, textY), uiColor(UiColor::Bad), inputError.c_str());
    else if (!listening) draw->AddText(fonts.text, 16 * s, ImVec2(left, textY), uiColor(UiColor::Dim), "No instrument: click the frets to play");
    menuScreenHint(state == State::Playing ? "Esc  back" : "Space  start    Esc  back", s);
    ImGui::Dummy(ImVec2(1, 1)); // the board moved ImGui's cursor (ui/fretboardview): an item after it
}
