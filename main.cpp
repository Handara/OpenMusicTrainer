#include "raylib.h"
#include <vector>
#include <cmath>

#define WINDOW_HEIGHT GetScreenHeight()
#define WINDOW_WIDTH GetScreenWidth()
#define G_CLEF_CODEPOINT 0xE050
#define NOTE_CODEPOINT 0xE1D5

float fontSize = 200;

int musicCodepoints[] = {
        G_CLEF_CODEPOINT, // G-clef
        0xE05C, // C-clef
        0xE062,  // F-clef
        NOTE_CODEPOINT // Single note
    };

float topStaffPercent = 3.0f/32.0f;
float bottomStaffPercent = 10.85f/32.0f;

void drawStaff(Font bravura, Vector2 position, int staffTopPosY, int staffWidth){
    DrawTextCodepoint(bravura, G_CLEF_CODEPOINT,position, fontSize, BLACK );
    DrawRectangleLines((int)position.x+150, (int)staffTopPosY-2*staffWidth, staffWidth*2, staffWidth*8, RED);
    for (int i=0; i < 5; i++){
        DrawLine((int)position.x, (int)staffTopPosY+i*staffWidth, (int)position.x+800, (int)staffTopPosY+i*staffWidth, BLACK);
    }
     for (int i=0; i < 12; i++){
        DrawTextCodepoint(bravura, NOTE_CODEPOINT,(Vector2){position.x+50+(50*i),(float)position.y-3*staffWidth+i*(staffWidth/2)}, fontSize, BLACK );
    }
}

struct Note {
    float time;       // seconds from pattern start when the note crosses the hit line
    int lane;         // string index, 0 = lowest string
    float hitFlash;   // seconds remaining to render as hit, 0 = not hit
    bool judged;       // true once this note has been hit or has missed
    bool wasPerfect;   // true if the judgement that set hitFlash was a perfect hit
};

struct GameState {
    int score;
    int combo;
    float rhythm; // 0..1, the "rhythm" meter
};

const int LANE_COUNT = 6;
const int LANE_SPACING = 70;
const int LANE_TOP_Y = 220;
const int HIT_LINE_X = 180;
const float SCROLL_SPEED = 300.0f; // pixels per second, placeholder until BPM-driven scroll lands
const float PERFECT_WINDOW_PX = 12.0f;
const float NEAR_WINDOW_PX = 30.0f;
const float HIT_FLASH_DURATION = 0.2f;
const float RHYTHM_FILL_PER_PERFECT = 0.12f;

const char* laneNames[LANE_COUNT] = { "E", "A", "D", "G", "B", "e" };
const int laneKeys[LANE_COUNT] = { KEY_ONE, KEY_TWO, KEY_THREE, KEY_FOUR, KEY_FIVE, KEY_SIX };
const Color laneColors[LANE_COUNT] = { RED, ORANGE, GOLD, GREEN, SKYBLUE, PURPLE };

const Color WOOD_DARK = { 61, 38, 27, 255 };
const Color WOOD_LIGHT = { 110, 70, 45, 255 };
const Color TRACK_PANEL = { 30, 18, 12, 200 };

int laneY(int lane){
    return LANE_TOP_Y + lane*LANE_SPACING;
}

int scoreMultiplier(int combo){
    return 1 + std::min(combo/10, 3); // caps at x4, like a Guitar Hero streak multiplier
}

void registerMiss(GameState& state){
    state.combo = 0;
    state.rhythm = 0.0f;
}

void registerHit(GameState& state, Note& note, float distancePx){
    if (distancePx <= PERFECT_WINDOW_PX){
        state.combo++;
        state.score += 100 * scoreMultiplier(state.combo);
        state.rhythm = std::min(1.0f, state.rhythm + RHYTHM_FILL_PER_PERFECT);
        note.wasPerfect = true;
    } else if (distancePx <= NEAR_WINDOW_PX){
        state.combo++;
        state.score += 10 * scoreMultiplier(state.combo);
        state.rhythm *= 0.5f;
        note.wasPerfect = false;
    } else {
        return; // outside the hittable window entirely, treat as a stray press
    }
    note.hitFlash = HIT_FLASH_DURATION;
    note.judged = true;
}

void handleInput(std::vector<Note>& notes, GameState& state, float patternTime){
    for (int lane = 0; lane < LANE_COUNT; lane++){
        if (!IsKeyPressed(laneKeys[lane])) continue;
        for (Note& note : notes){
            if (note.lane != lane || note.judged) continue;
            float x = HIT_LINE_X + (note.time - patternTime) * SCROLL_SPEED;
            registerHit(state, note, std::fabs(x - HIT_LINE_X));
        }
    }
}

void updateMisses(std::vector<Note>& notes, GameState& state, float patternTime){
    for (Note& note : notes){
        if (note.judged) continue;
        float x = HIT_LINE_X + (note.time - patternTime) * SCROLL_SPEED;
        if (x < HIT_LINE_X - NEAR_WINDOW_PX){
            registerMiss(state);
            note.judged = true;
        }
    }
}

void drawFretboard(const std::vector<Note>& notes, float patternTime){
    DrawRectangleGradientV(0, 0, WINDOW_WIDTH, WINDOW_HEIGHT, WOOD_DARK, WOOD_LIGHT);

    int panelTop = laneY(0) - 40;
    int panelHeight = laneY(LANE_COUNT-1) - laneY(0) + 80;
    DrawRectangle(0, panelTop, WINDOW_WIDTH, panelHeight, TRACK_PANEL);

    for (int i = 0; i < LANE_COUNT; i++){
        DrawLine(0, laneY(i), WINDOW_WIDTH, laneY(i), Fade(WHITE, 0.15f));
        DrawCircleLines(HIT_LINE_X, laneY(i), 22, Fade(laneColors[i], 0.8f));
        DrawText(TextFormat("%s [%d]", laneNames[i], i+1), 10, laneY(i)-10, 20, RAYWHITE);
    }
    DrawLine(HIT_LINE_X, panelTop, HIT_LINE_X, panelTop+panelHeight, GOLD);

    for (const Note& note : notes){
        float x = HIT_LINE_X + (note.time - patternTime) * SCROLL_SPEED;
        if (x > -50 && x < WINDOW_WIDTH + 50){
            Color color = laneColors[note.lane];
            if (note.hitFlash > 0.0f){
                float t = note.hitFlash / HIT_FLASH_DURATION;
                Color ringColor = note.wasPerfect ? WHITE : YELLOW;
                DrawCircleLines((int)x, laneY(note.lane), 22 + (1.0f-t)*20, Fade(ringColor, t));
                color = ringColor;
            }
            DrawCircle((int)x, laneY(note.lane), 16, color);
            DrawCircleLines((int)x, laneY(note.lane), 16, RAYWHITE);
        }
    }
}

void drawHUD(const GameState& state){
    const int barHeight = 16;
    DrawRectangle(0, 0, WINDOW_WIDTH, barHeight, Fade(BLACK, 0.5f));
    DrawRectangle(0, 0, (int)(WINDOW_WIDTH * state.rhythm), barHeight, ColorLerp(RED, GREEN, state.rhythm));
    DrawText("RHYTHM", 10, barHeight + 4, 14, Fade(RAYWHITE, 0.7f));

    const char* scoreText = TextFormat("%08d", state.score);
    int scoreWidth = MeasureText(scoreText, 36);
    DrawText(scoreText, WINDOW_WIDTH - scoreWidth - 20, barHeight + 10, 36, GOLD);

    const char* comboText = TextFormat("%d COMBO  x%d", state.combo, scoreMultiplier(state.combo));
    int comboWidth = MeasureText(comboText, 22);
    DrawText(comboText, WINDOW_WIDTH - comboWidth - 20, barHeight + 50, 22, RAYWHITE);
}

int main(void){
    InitWindow(WINDOW_WIDTH, WINDOW_HEIGHT, "OpenMusicTrainer");
    SetTargetFPS(60);

    std::vector<Note> notes = {
        {1.0f, 0}, {1.5f, 1}, {2.0f, 2}, {2.5f, 3},
        {3.0f, 4}, {3.5f, 5}, {4.5f, 0}, {5.0f, 2},
        {5.5f, 4}, {6.5f, 1}, {7.0f, 3}, {7.5f, 5},
    };
    const float patternDuration = 9.0f;
    GameState state = {0, 0, 0.0f};

    float elapsed = 0.0f;
    float prevPatternTime = 0.0f;
    while(!WindowShouldClose()){
        elapsed += GetFrameTime();
        float patternTime = std::fmod(elapsed, patternDuration);
        if (patternTime < prevPatternTime){
            for (Note& note : notes){
                note.hitFlash = 0.0f;
                note.judged = false;
            }
        }
        prevPatternTime = patternTime;

        for (Note& note : notes){
            if (note.hitFlash > 0.0f) note.hitFlash -= GetFrameTime();
        }
        handleInput(notes, state, patternTime);
        updateMisses(notes, state, patternTime);

        BeginDrawing();
        ClearBackground(RAYWHITE);
        drawFretboard(notes, patternTime);
        drawHUD(state);
        EndDrawing();
    }
    CloseWindow();
    return 0;
}
