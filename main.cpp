#include "raylib.h"
#include "imgui.h"
#include "rlImGui.h"
#include "audio.h"
#include "gameplay.h"
#include "songlibrary.h"
#include "tuner.h"
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

enum class Screen { MainMenu, SongSelect, Playing, Results, Tuner };

const float UI_FONT_SIZE = 26.0f;
const float TITLE_FONT_SIZE = 56.0f;
const float MENU_BUTTON_WIDTH = 420.0f;
const float MENU_BUTTON_HEIGHT = 56.0f;
const ImVec4 ERROR_TEXT_COLOR = { 1.0f, 0.45f, 0.4f, 1.0f };

const Color MENU_BG_TOP = { 28, 18, 14, 255 };
const Color MENU_BG_BOTTOM = { 70, 42, 28, 255 };

// App-wide state shared between screens
static struct {
    Screen screen = Screen::MainMenu;
    bool quit = false;
    std::string resourcesDir;
    std::vector<SongEntry> songs;
    std::string currentChartPath; // the song being played, kept for Retry
    std::string songSelectError;  // why the last song failed to start
    std::string mainMenuError;    // why the last main menu action failed (e.g. no input device)
    GameResult lastResult;
} app;

// --- Screen transitions -----------------------------------------------------------------------------

static void goToSongSelect(){
    app.songs = scanSongs(app.resourcesDir + "songs"); // rescan so newly added song folders show up
    app.screen = Screen::SongSelect;
}

static void startSong(const std::string& chartPath){
    std::string error;
    if (startGameplay(chartPath, error)){
        app.currentChartPath = chartPath;
        app.songSelectError.clear();
        app.screen = Screen::Playing;
    } else {
        app.songSelectError = error;
        app.screen = Screen::SongSelect;
    }
}

static void goToTuner(){
    std::string error;
    if (startTuner(error)){
        app.mainMenuError.clear();
        app.screen = Screen::Tuner;
    } else {
        app.mainMenuError = error;
    }
}

static void leaveTuner(){
    stopTuner();
    app.screen = Screen::MainMenu;
}

// Esc always means "back". Handled in one place so a single press can't trigger two transitions in one frame.
static void handleBackKey(){
    if (!IsKeyPressed(KEY_ESCAPE)) return;
    switch (app.screen){
        case Screen::MainMenu: break;
        case Screen::SongSelect: app.screen = Screen::MainMenu; break;
        case Screen::Playing: stopGameplay(); goToSongSelect(); break;
        case Screen::Results: goToSongSelect(); break;
        case Screen::Tuner: leaveTuner(); break;
    }
}

// --- Menu layout helpers ------------------------------------------------------------------------------
// Each menu is one invisible ImGui window covering the screen, with its content centered horizontally.

static void beginMenu(const char* id){
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove
                           | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoSavedSettings;
    ImGui::Begin(id, nullptr, flags);
    ImGui::Dummy(ImVec2(0, 60)); // top margin
}

static void centeredText(const char* text){
    ImGui::SetCursorPosX((ImGui::GetWindowWidth() - ImGui::CalcTextSize(text).x) / 2);
    ImGui::TextUnformatted(text);
}

static void menuTitle(const char* text){
    ImGui::PushFont(nullptr, TITLE_FONT_SIZE); // same font, bigger size
    centeredText(text);
    ImGui::PopFont();
    ImGui::Dummy(ImVec2(0, 30));
}

static bool menuButton(const char* label){
    ImGui::SetCursorPosX((ImGui::GetWindowWidth() - MENU_BUTTON_WIDTH) / 2);
    return ImGui::Button(label, ImVec2(MENU_BUTTON_WIDTH, MENU_BUTTON_HEIGHT));
}

static void centeredErrorText(const std::string& text){
    // Errors can be long (they include file paths), so wrap them to the button column's width
    float left = (ImGui::GetWindowWidth() - MENU_BUTTON_WIDTH) / 2;
    ImGui::SetCursorPosX(left);
    ImGui::PushStyleColor(ImGuiCol_Text, ERROR_TEXT_COLOR);
    ImGui::PushTextWrapPos(left + MENU_BUTTON_WIDTH);
    ImGui::TextUnformatted(text.c_str());
    ImGui::PopTextWrapPos();
    ImGui::PopStyleColor();
}

// --- Screens ------------------------------------------------------------------------------------------
// Immediate mode: these functions run every frame, drawing the widgets and reacting to clicks in the same call.

static void mainMenuScreen(){
    beginMenu("MainMenu");
    menuTitle("OpenMusicTrainer");
    if (menuButton("Play")) goToSongSelect();
    ImGui::SetItemDefaultFocus(); // keyboard navigation starts on Play

    if (menuButton("Tuner")) goToTuner();
    ImGui::BeginDisabled(); // not built yet
    menuButton("Editor (coming soon)");
    ImGui::EndDisabled();

    if (menuButton("Quit")) app.quit = true;
    if (!app.mainMenuError.empty()){
        ImGui::Dummy(ImVec2(0, 10));
        centeredErrorText(app.mainMenuError);
    }
    ImGui::End();
}

static void songSelectScreen(){
    beginMenu("SongSelect");
    menuTitle("Select a song");

    if (app.songs.empty()) centeredText("No songs found in Ressources/songs/");
    for (int i = 0; i < (int)app.songs.size(); i++){
        const SongEntry& song = app.songs[i];
        std::string label = song.artist.empty() ? song.title : song.title + "  -  " + song.artist;

        ImGui::PushID(i); // two songs with the same title must still be different widgets
        if (!song.error.empty()){
            ImGui::BeginDisabled();
            menuButton(label.c_str());
            ImGui::EndDisabled();
            centeredErrorText(song.error);
        } else if (menuButton(label.c_str())){
            startSong(song.chartPath);
        }
        if (i == 0) ImGui::SetItemDefaultFocus();
        ImGui::PopID();
    }

    if (!app.songSelectError.empty()){
        ImGui::Dummy(ImVec2(0, 10));
        centeredErrorText(app.songSelectError);
    }
    ImGui::Dummy(ImVec2(0, 20));
    if (menuButton("Back")) app.screen = Screen::MainMenu;
    ImGui::End();
}

static void resultsScreen(){
    const GameResult& r = app.lastResult;
    int hits = r.perfectCount + r.nearCount;
    float accuracy = r.totalNotes > 0 ? 100.0f * hits / r.totalNotes : 0.0f;

    beginMenu("Results");
    menuTitle(r.title.c_str());
    centeredText(TextFormat("Score  %08d", r.score));
    centeredText(TextFormat("Max combo  %d", r.maxCombo));
    centeredText(TextFormat("Perfect %d    Near %d    Miss %d", r.perfectCount, r.nearCount, r.missCount));
    centeredText(TextFormat("Notes hit  %d / %d  (%.1f%%)", hits, r.totalNotes, accuracy));
    ImGui::Dummy(ImVec2(0, 30));

    if (menuButton("Retry")) startSong(app.currentChartPath);
    ImGui::SetItemDefaultFocus();
    if (menuButton("Back to songs")) goToSongSelect();
    ImGui::End();
}

static void tunerScreen(){
    beginMenu("Tuner");
    drawTuner();
    ImGui::Dummy(ImVec2(0, 20));
    if (menuButton("Back")) leaveTuner();
    ImGui::SetItemDefaultFocus();
    ImGui::End();
}

static void setupImGui(){
    rlImGuiSetup(true);
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr; // don't write imgui.ini: menu layout is fixed in code
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard; // arrows + Enter work in menus

    std::string fontPath = app.resourcesDir + "fonts/Roboto-Medium.ttf";
    if (FileExists(fontPath.c_str())){
        io.FontDefault = io.Fonts->AddFontFromFileTTF(fontPath.c_str(), UI_FONT_SIZE);
    } else {
        TraceLog(LOG_WARNING, "UI font not found, using ImGui's default: %s", fontPath.c_str());
    }

    ImGuiStyle& style = ImGui::GetStyle();
    style.FontSizeBase = UI_FONT_SIZE;
    style.FrameRounding = 8.0f;
    style.ItemSpacing = ImVec2(12, 14);
    style.Colors[ImGuiCol_Button] = ImVec4(0.45f, 0.28f, 0.18f, 0.85f);
    style.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.62f, 0.40f, 0.24f, 1.0f);
    style.Colors[ImGuiCol_ButtonActive] = ImVec4(0.80f, 0.58f, 0.20f, 1.0f);
    style.Colors[ImGuiCol_NavCursor] = ImVec4(1.0f, 0.80f, 0.30f, 1.0f);
}

int main(void){
    const int INITIAL_WINDOW_WIDTH = 1280;
    const int INITIAL_WINDOW_HEIGHT = 720;

    // Resources are copied next to the executable at build time, so this works from any working directory
    app.resourcesDir = std::string(GetApplicationDirectory()) + "Ressources/";

    std::string error;
    if (!initAudio(error)){
        TraceLog(LOG_ERROR, "Audio: %s", error.c_str());
        return 1;
    }
    TraceLog(LOG_INFO, "Audio: using %s backend", audioBackendName());

    InitWindow(INITIAL_WINDOW_WIDTH, INITIAL_WINDOW_HEIGHT, "OpenMusicTrainer");
    SetTargetFPS(60);
    SetExitKey(KEY_NULL); // Esc means "back" (handleBackKey), not "quit"
    setupImGui();

    while (!WindowShouldClose() && !app.quit){
        handleBackKey();
        if (app.screen == Screen::Playing && !updateGameplay()){
            app.lastResult = gameplayResult();
            stopGameplay();
            app.screen = Screen::Results;
        }
        if (app.screen == Screen::Tuner) updateTuner();

        BeginDrawing();
        if (app.screen == Screen::Playing){
            drawGameplay();
        } else {
            DrawRectangleGradientV(0, 0, GetScreenWidth(), GetScreenHeight(), MENU_BG_TOP, MENU_BG_BOTTOM);
        }

        rlImGuiBegin();
        switch (app.screen){
            case Screen::MainMenu: mainMenuScreen(); break;
            case Screen::SongSelect: songSelectScreen(); break;
            case Screen::Playing: break; // gameplay draws with raylib only
            case Screen::Results: resultsScreen(); break;
            case Screen::Tuner: tunerScreen(); break;
        }
        rlImGuiEnd();
        EndDrawing();
    }

    stopGameplay();
    stopTuner();
    rlImGuiShutdown();
    CloseWindow();
    closeAudio();
    return 0;
}
