#include "raylib.h"
#include "audio.h"
#include "chart.h"
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

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
    int fret;
    float hitFlash;   // seconds remaining to render as hit, 0 = not hit
    bool judged;       // true once this note has been hit or has missed
    bool wasPerfect;   // true if the judgement that set hitFlash was a perfect hit
};

struct GameState {
    int score;
    int combo;
    int multiplier; // increments on a perfect hit, unchanged on near, resets on miss
    float rhythm; // 0..1, the "rhythm" meter
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

const char* pitchClassNames[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };

const Color WOOD_DARK = { 61, 38, 27, 255 };
const Color WOOD_LIGHT = { 110, 70, 45, 255 };
const Color TRACK_PANEL = { 30, 18, 12, 200 };

int laneY(int lane){
    return LANE_TOP_Y + lane*LANE_SPACING;
}

const int MAX_MULTIPLIER = 4;

void registerMiss(GameState& state){
    state.combo = 0;
    state.multiplier = 1;
    state.rhythm = 0.0f;
}

// timingError = note.time - press time: positive means early, negative means late
void registerHit(GameState& state, Note& note, float timingError){
    float absError = std::fabs(timingError);
    if (absError <= PERFECT_WINDOW_S){
        state.combo++;
        state.multiplier = std::min(state.multiplier + 1, MAX_MULTIPLIER);
        state.score += 100 * state.multiplier;
        state.rhythm = std::min(1.0f, state.rhythm + RHYTHM_FILL_PER_PERFECT);
        note.wasPerfect = true;
    } else if (absError <= NEAR_WINDOW_S){
        state.combo++;
        state.score += 10 * state.multiplier;
        state.rhythm *= 0.5f;
        note.wasPerfect = false;
    } else {
        return; // outside the hittable window entirely, treat as a stray press
    }
    note.hitFlash = HIT_FLASH_DURATION;
    note.judged = true;
}

void handleInput(std::vector<Note>& notes, GameState& state, float patternTime, int laneCount){
    for (int lane = 0; lane < laneCount; lane++){
        if (!IsKeyPressed(laneKeys[lane])) continue;

        // One press judges at most one note: the unjudged note in this lane closest in time
        Note* nearest = nullptr;
        float nearestError = 0.0f;
        for (Note& note : notes){
            if (note.lane != lane || note.judged) continue;
            float error = note.time - patternTime;
            if (nearest == nullptr || std::fabs(error) < std::fabs(nearestError)){
                nearest = &note;
                nearestError = error;
            }
        }
        if (nearest != nullptr) registerHit(state, *nearest, nearestError);
    }
}

void updateMisses(std::vector<Note>& notes, GameState& state, float patternTime){
    for (Note& note : notes){
        if (note.judged) continue;
        if (patternTime - note.time > NEAR_WINDOW_S){
            registerMiss(state);
            note.judged = true;
        }
    }
}

void drawFretboard(const std::vector<Note>& notes, float patternTime, const std::vector<int>& tuning){
    DrawRectangleGradientV(0, 0, GetScreenWidth(), GetScreenHeight(), WOOD_DARK, WOOD_LIGHT);

    int laneCount = (int)tuning.size();
    int panelTop = laneY(0) - 40;
    int panelHeight = laneY(laneCount-1) - laneY(0) + 80;
    DrawRectangle(0, panelTop, GetScreenWidth(), panelHeight, TRACK_PANEL);

    for (int i = 0; i < laneCount; i++){
        DrawLine(0, laneY(i), GetScreenWidth(), laneY(i), Fade(WHITE, 0.15f));
        DrawCircleLines(HIT_LINE_X, laneY(i), 22, Fade(laneColors[i], 0.8f));
        // MIDI pitch 60 is C4, so octave = pitch/12 - 1
        const char* label = TextFormat("%s%d [%d]", pitchClassNames[tuning[i] % 12], tuning[i]/12 - 1, i+1);
        DrawText(label, 10, laneY(i)-10, 20, RAYWHITE);
    }
    DrawLine(HIT_LINE_X, panelTop, HIT_LINE_X, panelTop+panelHeight, GOLD);

    for (const Note& note : notes){
        float x = HIT_LINE_X + (note.time - patternTime) * SCROLL_SPEED;
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

void drawHUD(const GameState& state){
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

int main(void){
    const int INITIAL_WINDOW_WIDTH = 1280;
    const int INITIAL_WINDOW_HEIGHT = 720;

    // Resources are copied next to the executable at build time, so this works from any working directory
    std::string songDir = std::string(GetApplicationDirectory()) + "Ressources/songs/test-pattern/";
    std::string chartPath = songDir + "song.chart";
    Chart chart;
    std::string error;
    if (!loadChart(chartPath, chart, error)){
        TraceLog(LOG_ERROR, "Failed to load chart: %s", error.c_str());
        return 1;
    }
    if (chart.audioFile.empty()){
        TraceLog(LOG_ERROR, "Chart has no 'audio' line: %s", chartPath.c_str());
        return 1;
    }
    const FrettedTrack& track = chart.frettedTracks[0];
    if ((int)track.tuning.size() > MAX_LANES){
        TraceLog(LOG_ERROR, "Track '%s' has %d strings, the prototype supports up to %d",
                 track.name.c_str(), (int)track.tuning.size(), MAX_LANES);
        return 1;
    }

    // Gameplay notes: the chart's notes converted to seconds, plus per-run judging state
    std::vector<Note> notes;
    notes.reserve(track.notes.size());
    for (const FrettedNote& chartNote : track.notes){
        notes.push_back({(float)tickToSeconds(chart, chartNote.tick), chartNote.stringIndex, chartNote.fret});
    }

    if (!initAudio(error) || !loadSong(songDir + chart.audioFile, error)){
        TraceLog(LOG_ERROR, "Audio: %s", error.c_str());
        closeAudio();
        return 1;
    }
    TraceLog(LOG_INFO, "Audio: using %s backend, song is %.2f s", audioBackendName(), songLength());

    InitWindow(INITIAL_WINDOW_WIDTH, INITIAL_WINDOW_HEIGHT, "OpenMusicTrainer");
    SetTargetFPS(60);

    GameState state = {0, 0, 1, 0.0f};

    playSong(true);
    float prevPatternTime = 0.0f;
    while(!WindowShouldClose()){
        // The song's playback position is the clock: notes stay in sync with the music even if frames stutter
        float patternTime = (float)songPosition();
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
        handleInput(notes, state, patternTime, (int)track.tuning.size());
        updateMisses(notes, state, patternTime);

        BeginDrawing();
        ClearBackground(RAYWHITE);
        drawFretboard(notes, patternTime, track.tuning);
        drawHUD(state);
        EndDrawing();
    }
    closeAudio();
    CloseWindow();
    return 0;
}
