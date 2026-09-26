#include "raylib.h"
#include "audio/audio.h"
#include "core/songlibrary.h"
#include "screens/gameplay.h"
#include "screens/menus.h"
#include "screens/tuner.h"
#include "ui/ui.h"

#include <string>
#include <vector>

enum class Screen { MainMenu, SongSelect, Playing, Results, Tuner };

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

// Shows the current menu screen and acts on the player's choice: this is the whole flow of the app
static void runMenus(){
    switch (app.screen){
        case Screen::MainMenu:
            switch (mainMenuScreen(app.mainMenuError)){
                case MainMenuChoice::Play: goToSongSelect(); break;
                case MainMenuChoice::Tuner: goToTuner(); break;
                case MainMenuChoice::Quit: app.quit = true; break;
                case MainMenuChoice::None: break;
            }
            break;
        case Screen::SongSelect: {
            SongSelectChoice choice = songSelectScreen(app.songs, app.songSelectError);
            if (choice.back) app.screen = Screen::MainMenu;
            else if (choice.songIndex >= 0) startSong(app.songs[choice.songIndex].chartPath);
            break;
        }
        case Screen::Results:
            switch (resultsScreen(app.lastResult)){
                case ResultsChoice::Retry: startSong(app.currentChartPath); break;
                case ResultsChoice::BackToSongs: goToSongSelect(); break;
                case ResultsChoice::None: break;
            }
            break;
        case Screen::Tuner:
            if (tunerScreen()) leaveTuner();
            break;
        case Screen::Playing: break; // gameplay draws with raylib only
    }
}

int main(void){
    const int INITIAL_WINDOW_WIDTH = 1280;
    const int INITIAL_WINDOW_HEIGHT = 720;

    // Resources are copied next to the executable at build time, so this works from any working directory
    app.resourcesDir = std::string(GetApplicationDirectory()) + "resources/";

    std::string error;
    if (!initAudio(error)){
        TraceLog(LOG_ERROR, "Audio: %s", error.c_str());
        return 1;
    }
    TraceLog(LOG_INFO, "Audio: using %s backend", audioBackendName());

    InitWindow(INITIAL_WINDOW_WIDTH, INITIAL_WINDOW_HEIGHT, "OpenMusicTrainer");
    SetTargetFPS(60);
    SetExitKey(KEY_NULL); // Esc means "back" (handleBackKey), not "quit"
    initUi(app.resourcesDir + "fonts/Roboto-Medium.ttf");

    while (!WindowShouldClose() && !app.quit){
        handleBackKey();
        if (app.screen == Screen::Playing && !updateGameplay()){
            app.lastResult = gameplayResult();
            stopGameplay();
            app.screen = Screen::Results;
        }
        if (app.screen == Screen::Tuner) updateTuner();

        BeginDrawing();
        if (app.screen == Screen::Playing) drawGameplay();
        else drawMenuBackground();

        beginUiFrame();
        runMenus();
        endUiFrame();
        EndDrawing();
    }

    stopGameplay();
    stopTuner();
    closeUi();
    CloseWindow();
    closeAudio();
    return 0;
}
