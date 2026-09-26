#include "screens/gameplay.h"

#include "audio/audio.h"
#include "core/chart.h"
#include "core/music.h"
#include "raylib.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <vector>

struct Note {
    float time;       // song time in seconds when the note crosses the hit line
    int lane;         // string index, 0 = lowest string
    int fret;
    float hitFlash = 0.0f;   // seconds remaining to render as hit, 0 = not hit
    bool judged = false;     // true once this note has been hit or has missed
    bool wasPerfect = false; // true if the judgement that set hitFlash was a perfect hit
};

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
const int LANE_SPACING = 70;
const int LANE_TOP_Y = 220;
const int HIT_LINE_X = 180;
const float SCROLL_SPEED = 300.0f; // pixels per second, placeholder until BPM-driven scroll lands
const float PERFECT_WINDOW_S = 0.040f; // max |timing error| in seconds for a perfect hit
const float NEAR_WINDOW_S = 0.100f;
const float HIT_FLASH_DURATION = 0.2f;
const float RHYTHM_FILL_PER_PERFECT = 0.12f;

const int laneKeys[MAX_LANES] = { KEY_ONE, KEY_TWO, KEY_THREE, KEY_FOUR, KEY_FIVE, KEY_SIX };
const Color laneColors[MAX_LANES] = { RED, ORANGE, GOLD, GREEN, SKYBLUE, PURPLE };

const Color WOOD_DARK = { 61, 38, 27, 255 };
const Color WOOD_LIGHT = { 110, 70, 45, 255 };
const Color TRACK_PANEL = { 30, 18, 12, 200 };

static int laneY(int lane){
    return LANE_TOP_Y + lane*LANE_SPACING;
}

const int MAX_MULTIPLIER = 4;

static void registerMiss(GameState& state){
    state.combo = 0;
    state.multiplier = 1;
    state.missCount++;
    state.rhythm = 0.0f;
}

// timingError = note.time - press time: positive means early, negative means late
static void registerHit(GameState& state, Note& note, float timingError){
    float absError = std::fabs(timingError);
    if (absError <= PERFECT_WINDOW_S){
        state.combo++;
        state.multiplier = std::min(state.multiplier + 1, MAX_MULTIPLIER);
        state.score += 100 * state.multiplier;
        state.rhythm = std::min(1.0f, state.rhythm + RHYTHM_FILL_PER_PERFECT);
        state.perfectCount++;
        note.wasPerfect = true;
    } else if (absError <= NEAR_WINDOW_S){
        state.combo++;
        state.score += 10 * state.multiplier;
        state.rhythm *= 0.5f;
        state.nearCount++;
        note.wasPerfect = false;
    } else {
        return; // outside the hittable window entirely, treat as a stray press
    }
    state.maxCombo = std::max(state.maxCombo, state.combo);
    note.hitFlash = HIT_FLASH_DURATION;
    note.judged = true;
}

static void handleInput(std::vector<Note>& notes, GameState& state, float songTime, int laneCount){
    for (int lane = 0; lane < laneCount; lane++){
        if (!IsKeyPressed(laneKeys[lane])) continue;

        // One press judges at most one note: the unjudged note in this lane closest in time
        Note* nearest = nullptr;
        float nearestError = 0.0f;
        for (Note& note : notes){
            if (note.lane != lane || note.judged) continue;
            float error = note.time - songTime;
            if (nearest == nullptr || std::fabs(error) < std::fabs(nearestError)){
                nearest = &note;
                nearestError = error;
            }
        }
        if (nearest != nullptr) registerHit(state, *nearest, nearestError);
    }
}

static void updateMisses(std::vector<Note>& notes, GameState& state, float songTime){
    for (Note& note : notes){
        if (note.judged) continue;
        if (songTime - note.time > NEAR_WINDOW_S){
            registerMiss(state);
            note.judged = true;
        }
    }
}

static void drawFretboard(const std::vector<Note>& notes, float songTime, const std::vector<int>& tuning){
    DrawRectangleGradientV(0, 0, GetScreenWidth(), GetScreenHeight(), WOOD_DARK, WOOD_LIGHT);

    int laneCount = (int)tuning.size();
    int panelTop = laneY(0) - 40;
    int panelHeight = laneY(laneCount-1) - laneY(0) + 80;
    DrawRectangle(0, panelTop, GetScreenWidth(), panelHeight, TRACK_PANEL);

    for (int i = 0; i < laneCount; i++){
        DrawLine(0, laneY(i), GetScreenWidth(), laneY(i), Fade(WHITE, 0.15f));
        DrawCircleLines(HIT_LINE_X, laneY(i), 22, Fade(laneColors[i], 0.8f));
        const char* label = TextFormat("%s%d [%d]", pitchClassName(tuning[i]), pitchOctave(tuning[i]), i+1);
        DrawText(label, 10, laneY(i)-10, 20, RAYWHITE);
    }
    DrawLine(HIT_LINE_X, panelTop, HIT_LINE_X, panelTop+panelHeight, GOLD);

    for (const Note& note : notes){
        float x = HIT_LINE_X + (note.time - songTime) * SCROLL_SPEED;
        if (x > -50 && x < GetScreenWidth() + 50){
            Color color = laneColors[note.lane];
            if (note.hitFlash > 0.0f){
                float t = note.hitFlash / HIT_FLASH_DURATION;
                Color ringColor = note.wasPerfect ? WHITE : YELLOW;
                DrawCircleLines((int)x, laneY(note.lane), 22 + (1.0f-t)*20, Fade(ringColor, t));
                color = ringColor;
            }
            DrawCircle((int)x, laneY(note.lane), 16, color);
            DrawCircleLines((int)x, laneY(note.lane), 16, RAYWHITE);

            const char* fretText = TextFormat("%d", note.fret);
            int fretWidth = MeasureText(fretText, 20);
            DrawText(fretText, (int)x - fretWidth/2, laneY(note.lane) - 10, 20, BLACK);
        }
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
    std::vector<Note> notes;
    GameState state;
    float songTime = 0.0f;
    bool active = false;
} game;

bool startGameplay(const std::string& chartPath, std::string& error){
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
        game.notes.push_back({(float)tickToSeconds(game.chart, chartNote.tick), chartNote.stringIndex, chartNote.fret});
    }
    game.state = {};
    game.state.multiplier = 1;
    game.songTime = 0.0f;
    game.active = true;
    playSong(false);
    return true;
}

bool updateGameplay(){
    if (!game.active) return false;

    // The song's playback position is the clock: notes stay in sync with the music even if frames stutter
    game.songTime = (float)songPosition();

    for (Note& note : game.notes){
        if (note.hitFlash > 0.0f) note.hitFlash -= GetFrameTime();
    }
    const FrettedTrack& track = game.chart.frettedTracks[0];
    handleInput(game.notes, game.state, game.songTime, (int)track.tuning.size());
    updateMisses(game.notes, game.state, game.songTime);

    if (songEnded()){
        // Anything still unjudged when the music stops counts as missed
        for (Note& note : game.notes){
            if (!note.judged){
                registerMiss(game.state);
                note.judged = true;
            }
        }
        return false;
    }
    return true;
}

void drawGameplay(){
    if (!game.active) return;
    drawFretboard(game.notes, game.songTime, game.chart.frettedTracks[0].tuning);
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
