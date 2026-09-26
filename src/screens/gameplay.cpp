#include "screens/gameplay.h"

#include "audio/audio.h"
#include "core/chart.h"
#include "raylib.h"
#include "views/highway.h"
#include "core/judge.h"
#include "views/playnote.h"
#include "views/staff.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <vector>

struct GameState {
    int score;
    int combo;
    int maxCombo;
    int multiplier; // increments on a perfect hit, unchanged on near, resets on miss
    float rhythm; // 0..1, the "rhythm" meter
    int perfectCount;
    int nearCount;
    int missCount;
};

const int MAX_LANES = 6; // limited by the number keys and the vertical layout for now
const int HIT_LINE_X = 180;
const float RHYTHM_FILL_PER_PERFECT = 0.12f;

const int laneKeys[MAX_LANES] = { KEY_ONE, KEY_TWO, KEY_THREE, KEY_FOUR, KEY_FIVE, KEY_SIX };

const Color WOOD_DARK = { 61, 38, 27, 255 };
const Color WOOD_LIGHT = { 110, 70, 45, 255 };
const int MAX_MULTIPLIER = 4;

static void scoreMisses(GameState& state, int count){
    if (count == 0) return;
    state.combo = 0;
    state.multiplier = 1;
    state.missCount += count;
    state.rhythm = 0.0f;
}

// Scoring is the game's own rule on top of judging: perfect hits build the multiplier, near hits don't
static void scoreHit(GameState& state, Judgement judgement, int notesHit){
    for (int i = 0; i < notesHit; i++){
        state.combo++;
        if (judgement == Judgement::Perfect){
            state.multiplier = std::min(state.multiplier + 1, MAX_MULTIPLIER);
            state.score += 100 * state.multiplier;
            state.rhythm = std::min(1.0f, state.rhythm + RHYTHM_FILL_PER_PERFECT);
            state.perfectCount++;
        } else {
            state.score += 10 * state.multiplier;
            state.rhythm *= 0.5f;
            state.nearCount++;
        }
    }
    state.maxCombo = std::max(state.maxCombo, state.combo);
}

// Number keys 1 to 6 stand for the strings, lowest first
static void handleKeyboard(std::vector<PlayNote>& notes, GameState& state, float songTime, int laneCount){
    for (int lane = 0; lane < laneCount; lane++){
        if (!IsKeyPressed(laneKeys[lane])) continue;
        PlayerInput press;
        press.time = songTime;
        press.stringIndex = lane;
        JudgeResult result = judgeInput(notes, press);
        if (result.judgement != Judgement::Ignored) scoreHit(state, result.judgement, result.notesHit);
    }
}

// Where each view goes on screen, as fractions of the window height, for each note view setting
static void layoutViews(NoteView view, Rectangle& staffArea, Rectangle& highwayArea){
    float width = (float)GetScreenWidth(), height = (float)GetScreenHeight();
    staffArea = highwayArea = {0, 0, 0, 0};
    switch (view){
        case NoteView::Highway: highwayArea = {0, height * 0.25f, width, height * 0.60f}; break;
        case NoteView::Staff:   staffArea = {0, height * 0.20f, width, height * 0.60f}; break;
        case NoteView::Both:
            staffArea = {0, height * 0.14f, width, height * 0.30f};
            highwayArea = {0, height * 0.46f, width, height * 0.52f};
            break;
    }
}

static void drawHUD(const GameState& state){
    const int barHeight = 16;
    DrawRectangle(0, 0, GetScreenWidth(), barHeight, Fade(BLACK, 0.5f));
    DrawRectangle(0, 0, (int)(GetScreenWidth() * state.rhythm), barHeight, ColorLerp(RED, GREEN, state.rhythm));
    DrawText("RHYTHM", 10, barHeight + 4, 14, Fade(RAYWHITE, 0.7f));

    const char* scoreText = TextFormat("%08d", state.score);
    int scoreWidth = MeasureText(scoreText, 36);
    DrawText(scoreText, GetScreenWidth() - scoreWidth - 20, barHeight + 10, 36, GOLD);

    const char* comboText = TextFormat("%d COMBO  x%d", state.combo, state.multiplier);
    int comboWidth = MeasureText(comboText, 22);
    DrawText(comboText, GetScreenWidth() - comboWidth - 20, barHeight + 50, 22, RAYWHITE);
}

// Everything the play screen needs while a song is running
static struct {
    Chart chart;
    std::vector<PlayNote> notes;
    std::vector<float> barTimes; // song times where bars start, for the staff's bar lines
    GameState state;
    GameplayOptions options;
    float songTime = 0.0f;
    bool active = false;
} game;

bool startGameplay(const std::string& chartPath, const GameplayOptions& options, std::string& error){
    stopGameplay();
    if (!loadChart(chartPath, game.chart, error)) return false;
    if (game.chart.audioFile.empty()){
        error = chartPath + ": chart has no 'audio' line";
        return false;
    }
    const FrettedTrack& track = game.chart.frettedTracks[0];
    if ((int)track.tuning.size() > MAX_LANES){
        error = "track '" + track.name + "' has " + std::to_string(track.tuning.size())
                + " strings, the prototype supports up to " + std::to_string(MAX_LANES);
        return false;
    }

    // The audio file is named relative to the chart's own folder
    std::filesystem::path audioPath = std::filesystem::path(chartPath).parent_path() / game.chart.audioFile;
    if (!loadSong(audioPath.string(), error)) return false;

    // Gameplay notes: the chart's notes converted to seconds, plus per-run judging state
    game.notes.clear();
    game.notes.reserve(track.notes.size());
    for (const FrettedNote& chartNote : track.notes){
        int pitch = track.tuning[chartNote.stringIndex] + chartNote.fret;
        game.notes.push_back({(float)tickToSeconds(game.chart, chartNote.tick), chartNote.stringIndex, chartNote.fret, pitch});
    }
    const int ticksPerBar = game.chart.resolution * 4; // 4/4 until charts have time signatures
    game.barTimes.clear();
    for (int tick = 0; tick <= game.chart.endTick; tick += ticksPerBar) game.barTimes.push_back((float)tickToSeconds(game.chart, tick));
    game.state = {};
    game.state.multiplier = 1;
    game.options = options;
    game.songTime = 0.0f;
    game.active = true;
    playSong(false);
    return true;
}

bool updateGameplay(){
    if (!game.active) return false;

    // The song's playback position is the clock: notes stay in sync with the music even if frames stutter
    // The offset shifts the whole game against the audio: if sound reaches your ears late (Bluetooth,
    // slow drivers), a positive offset moves notes and judging later to match what you hear
    game.songTime = (float)(songPosition() - game.options.offsetSeconds);

    for (PlayNote& note : game.notes){
        if (note.hitFlash > 0.0f) note.hitFlash -= GetFrameTime();
    }
    const FrettedTrack& track = game.chart.frettedTracks[0];
    handleKeyboard(game.notes, game.state, game.songTime, (int)track.tuning.size());
    scoreMisses(game.state, markMisses(game.notes, game.songTime));

    if (songEnded()){
        // Anything still unjudged when the music stops counts as missed
        scoreMisses(game.state, markMisses(game.notes, game.songTime + 1e9));
        return false;
    }
    return true;
}

void drawGameplay(){
    if (!game.active) return;
    DrawRectangleGradientV(0, 0, GetScreenWidth(), GetScreenHeight(), WOOD_DARK, WOOD_LIGHT);
    TimeAxis axis = { game.songTime, (float)HIT_LINE_X, game.options.noteSpeed };
    Rectangle staffArea, highwayArea;
    layoutViews(game.options.noteView, staffArea, highwayArea);
    if (staffArea.height > 0) drawStaff(staffArea, game.notes, game.barTimes, axis);
    if (highwayArea.height > 0){
        drawHighway(highwayArea, game.notes, game.chart.frettedTracks[0].tuning, game.options.lowStringOnTop, axis);
    }
    drawHUD(game.state);
}

void stopGameplay(){
    unloadSong();
    game.active = false;
}

GameResult gameplayResult(){
    GameResult result;
    result.title = game.chart.title;
    result.score = game.state.score;
    result.maxCombo = game.state.maxCombo;
    result.perfectCount = game.state.perfectCount;
    result.nearCount = game.state.nearCount;
    result.missCount = game.state.missCount;
    result.totalNotes = (int)game.notes.size();
    return result;
}
