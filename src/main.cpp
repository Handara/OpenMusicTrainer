#include "raylib.h"
#include "audio/audio.h"
#include "core/paths.h"
#include "core/songlibrary.h"
#include "screens/editor.h"
#include "screens/gameplay.h"
#include "screens/menus.h"
#include "screens/tuner.h"
#include "ui/ui.h"

#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;

enum class Screen { MainMenu, SongSelect, Playing, Results, Tuner, EditorSelect, Editor };

// App-wide state shared between screens
static struct {
    Screen screen = Screen::MainMenu;
    bool quit = false;
    std::string resourcesDir;     // shipped with the game, read-only
    std::string userDataDir;      // the player's own files (see core/paths.h)
    std::string userSongsDir;
    std::vector<SongEntry> songs;
    std::string currentChartPath; // the song being played, kept for Retry
    std::string songSelectError;  // why the last song failed to start
    std::string mainMenuError;    // why the last main menu action failed (e.g. no input device)
    GameResult lastResult;
} app;

// --- Screen transitions -----------------------------------------------------------------------------

// Built-in songs first, then the player's own. Rescanned on every visit so new song folders show up.
static void goToSongList(Screen listScreen){
    app.songs = scanSongs(app.resourcesDir + "songs", true);
    std::vector<SongEntry> userSongs = scanSongs(app.userSongsDir, false);
    app.songs.insert(app.songs.end(), userSongs.begin(), userSongs.end());
    app.screen = listScreen;
}

static void goToSongSelect(){
    goToSongList(Screen::SongSelect);
}

static void openDataFolder(){
    // raylib's OpenURL hands the path to the system (Explorer, Finder, xdg-open), which opens folders too.
    // make_preferred() gives Explorer the backslashes it expects on Windows.
    std::string path = fs::path(app.userDataDir).make_preferred().string();
    OpenURL(path.c_str());
}

static void editSong(const SongEntry& song){
    std::string error;
    if (openEditor(song, app.userSongsDir, error)){
        app.songSelectError.clear();
        app.screen = Screen::Editor;
    } else {
        app.songSelectError = error;
    }
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
        case Screen::EditorSelect: app.screen = Screen::MainMenu; break;
        case Screen::Editor: break; // the editor handles Esc itself, to warn about unsaved changes
    }
}

// Shows the current menu screen and acts on the player's choice: this is the whole flow of the app
static void runMenus(){
    switch (app.screen){
        case Screen::MainMenu:
            switch (mainMenuScreen(app.mainMenuError)){
                case MainMenuChoice::Play: goToSongSelect(); break;
                case MainMenuChoice::Editor: goToSongList(Screen::EditorSelect); break;
                case MainMenuChoice::Tuner: goToTuner(); break;
                case MainMenuChoice::Quit: app.quit = true; break;
                case MainMenuChoice::None: break;
            }
            break;
        case Screen::SongSelect: {
            SongSelectChoice choice = songSelectScreen("Select a song", app.songs, app.songSelectError, false);
            if (choice.back) app.screen = Screen::MainMenu;
            else if (choice.openDataFolder) openDataFolder();
            else if (choice.songIndex >= 0) startSong(app.songs[choice.songIndex].chartPath);
            break;
        }
        case Screen::EditorSelect: {
            SongSelectChoice choice = songSelectScreen("Edit a song", app.songs, app.songSelectError, true);
            if (choice.back) app.screen = Screen::MainMenu;
            else if (choice.openDataFolder) openDataFolder();
            else if (choice.songIndex >= 0) editSong(app.songs[choice.songIndex]);
            break;
        }
        case Screen::Editor:
            if (editorScreen() == EditorChoice::Back){
                closeEditor();
                goToSongList(Screen::EditorSelect); // rescan: saving a built-in song created a new one
            }
            break;
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
    app.userDataDir = userDataDir();
    app.userSongsDir = (fs::path(app.userDataDir) / "songs").string();
    std::error_code ec;
    fs::create_directories(app.userSongsDir, ec);
    if (ec) TraceLog(LOG_WARNING, "Could not create %s: %s", app.userSongsDir.c_str(), ec.message().c_str());
    TraceLog(LOG_INFO, "User data folder: %s", app.userDataDir.c_str());

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
    closeEditor();
    closeUi();
    CloseWindow();
    closeAudio();
    return 0;
}
