#include "raylib.h"
#include "app/crashreport.h"
#include "audio/audio.h"
#include "core/paths.h"
#include "core/routine.h"
#include "core/settings.h"
#include "core/songlibrary.h"
#include "core/songpackage.h"
#include "screens/calibration.h"
#include "screens/editor.h"
#include "screens/gameplay.h"
#include "screens/instrumentscreen.h"
#include "screens/learnscreen.h"
#include "screens/lessoneditor.h"
#include "screens/mainmenu.h"
#include "screens/menus.h"
#include "screens/newsong.h"
#include "screens/settingsscreen.h"
#include "screens/tuner.h"
#include "ui/transition.h"
#include "ui/ui.h"
#include "views/staff.h"
#include "views/viewfont.h"

#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;

enum class Screen { MainMenu, SongSelect, Playing, Results, Tuner, Instrument, EditorSelect, NewSong, Editor, LessonEditor, Settings, Learn, Calibration };

// App-wide state shared between screens
static struct {
    Screen screen = Screen::MainMenu;
    bool quit = false;
    std::string resourcesDir;     // shipped with the game, read-only
    std::string userDataDir;      // the player's own files (see core/paths.h)
    std::string userSongsDir;
    std::string soundsDir;        // the player's own preview sounds
    std::string progressDir;      // learn mode progress
    std::string userExercisesDir; // the player's own learn mode exercises
    std::string userLessonsDir;   // the player's own lessons, one folder each
    std::string settingsPath;
    Settings settings;
    std::vector<SongEntry> songs;
    SongEntry currentSong;        // the song being played, kept for Retry and its records
    int currentPart = 0;          // and which of its parts
    bool currentRhythm = false;   // and in which mode
    std::string songSelectError;  // why the last song failed to start (or a package failed to install)
    std::string songSelectNotice; // a song package just installed
    std::string packagesDir;      // song packages the player made, to share
    std::string mainMenuError;    // why the last main menu action failed (e.g. no input device)
    GameResult lastResult;
    Vector2 zoomTo = {-1.0f, -1.0f}; // the next change of screen zooms into this point (the end of a song)
    bool testPlaying = false;     // playing the editor's chart: the end or Esc goes back to the editor
    CalibrationMode calibrationMode = CalibrationMode::Tap;
    std::string settingsError;    // why calibration couldn't start (e.g. no input device)
} app;

// --- Screen transitions -----------------------------------------------------------------------------

static std::string recordsDir(){
    return (fs::path(app.progressDir) / "records").string();
}

// Built-in songs first, then the player's own. Rescanned on every visit so new song folders show up.
static void goToSongList(Screen listScreen){
    if (app.screen != listScreen) app.songSelectNotice.clear(); // news is for the visit it happened in
    app.songs = scanSongs(app.resourcesDir + "songs", true);
    std::vector<SongEntry> userSongs = scanSongs(app.userSongsDir, false);
    app.songs.insert(app.songs.end(), userSongs.begin(), userSongs.end());
    loadBestRuns(app.songs, recordsDir());
    app.screen = listScreen;
}

// Song packages (.lahn) dropped on a song list are installed into the player's songs, and the list shows them
static void installDroppedPackages(){
    if (!IsFileDropped()) return;
    FilePathList dropped = LoadDroppedFiles();
    std::vector<std::string> added;
    app.songSelectError.clear();
    for (unsigned i = 0; i < dropped.count; i++){
        std::string path = dropped.paths[i], folder, error;
        if (fs::path(path).extension() != SONG_PACKAGE_EXTENSION){
            app.songSelectError = fs::path(path).filename().string() + " isn't a song package (a .lahn file)";
        } else if (installSongPackage(path, app.userSongsDir, folder, error)){
            added.push_back(fs::path(folder).filename().string());
        } else {
            app.songSelectError = error;
        }
    }
    UnloadDroppedFiles(dropped);
    app.songSelectNotice.clear();
    for (const std::string& name : added) app.songSelectNotice += (app.songSelectNotice.empty() ? "Added: " : ", ") + name;
    goToSongList(app.screen); // rescan: the new songs show up
}

static void goToSongSelect(){
    goToSongList(Screen::SongSelect);
}

static void openDataFolder(){
    openFolder(app.userDataDir);
}

static void goToSettings(){
    openSettingsScreen(app.soundsDir);
    app.settingsError.clear();
    app.screen = Screen::Settings;
}

static void goToCalibration(CalibrationMode mode){
    std::string error;
    if (startCalibration(mode, app.settings, error)){
        app.calibrationMode = mode;
        app.settingsError.clear();
        app.screen = Screen::Calibration;
    } else {
        app.settingsError = "Can't calibrate: " + error;
    }
}

static void leaveCalibration(){
    stopCalibration();
    app.screen = Screen::Settings;
}

static void saveAppSettings(){
    std::string error;
    if (!saveSettings(app.settingsPath, app.settings, error)) TraceLog(LOG_WARNING, "Settings: %s", error.c_str());
}

static void leaveSettings(){
    closeSettingsScreen();
    saveAppSettings();
    app.screen = Screen::MainMenu;
}

static void editSong(const SongEntry& song){
    std::string error;
    if (openEditor(song, app.userSongsDir, app.packagesDir, app.settings.lowStringOnTop, error)){
        app.songSelectError.clear();
        app.screen = Screen::Editor;
    } else {
        app.songSelectError = error;
    }
}

// A finished run goes into its part's records (not a test-play from the editor: that isn't a real run). The
// result then knows where it placed, and shows the part's best runs.
static void recordRun(GameResult& result){
    std::error_code ec;
    fs::create_directories(recordsDir(), ec);
    std::string path = recordsPath(recordsDir(), songId(app.currentSong), app.currentPart, result.fingerprint); // "-rhythm" in rhythm mode

    RunRecord run;
    run.score = result.score;
    run.accuracy = result.accuracy;
    run.maxCombo = result.maxCombo;
    run.perfect = result.perfectCount;
    run.good = result.nearCount;
    run.miss = result.missCount;
    run.unstableRate = result.timing.unstableRate;
    run.withInstrument = result.withInstrument;
    int year, month, day;
    dateFromDays(today(), year, month, day);
    run.date = TextFormat("%04d-%02d-%02d", year, month, day);
    std::vector<RunRecord> records = loadRuns(path);
    result.place = addRun(records, run);
    std::string error;
    if (result.place >= 0 && !saveRuns(path, records, error)) TraceLog(LOG_WARNING, "Records: %s", error.c_str());
    result.records = records;
}

static GameplayOptions gameplayOptions(){
    GameplayOptions options;
    options.noteSpeed = app.settings.noteSpeed;
    options.offsetSeconds = app.settings.globalOffsetMs / 1000.0f;
    options.lowStringOnTop = app.settings.lowStringOnTop;
    options.noteViews = app.settings.noteViews;
    options.playWithInstrument = app.settings.playWithInstrument;
    options.inputDevice = app.settings.inputDevice;
    options.guitarChannel = app.settings.guitarChannel;
    options.bassChannel = app.settings.bassChannel;
    options.midiDevice = app.settings.midiDevice;
    options.pianoKeys = app.settings.pianoKeys;
    options.inputOffsetSeconds = app.settings.inputOffsetMs / 1000.0f;
    options.hitSounds = !app.settings.playWithInstrument;
    return options;
}

static void startSong(const SongEntry& song, int part, bool rhythmMode){
    std::string error;
    app.testPlaying = false;
    GameplayOptions options = gameplayOptions();
    options.part = part;
    options.rhythmMode = rhythmMode;
    if (startGameplay(song.chartPath, options, error)){
        app.currentRhythm = rhythmMode;
        app.currentSong = song;
        app.currentPart = part;
        app.songSelectError.clear();
        app.screen = Screen::Playing;
    } else {
        app.songSelectError = error;
        app.screen = Screen::SongSelect;
    }
}

static void startTestPlay(){
    EditorTestPlay test = editorTestPlay();
    std::string error;
    GameplayOptions options = gameplayOptions();
    options.part = test.part;
    if (startGameplayWithChart(test.chart, test.audioPath, options, test.fromTick, error)){
        app.testPlaying = true;
        app.screen = Screen::Playing;
    } else {
        resumeEditor("Test play: " + error);
    }
}

static void backToEditor(){
    stopGameplay();
    app.testPlaying = false;
    resumeEditor();
    app.screen = Screen::Editor;
}

static void goToTuner(){
    std::string error;
    if (startTuner(app.settings.inputDevice, error)){
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

static void leaveInstrument(){
    closeInstrumentScreen();
    app.screen = Screen::MainMenu;
}

// Esc always means "back". Handled in one place so a single press can't trigger two transitions in one frame.
static void handleBackKey(){
    if (!IsKeyPressed(KEY_ESCAPE)) return;
    switch (app.screen){
        case Screen::MainMenu: break;
        case Screen::SongSelect: if (!songSelectBack()) app.screen = Screen::MainMenu; break;
        case Screen::Playing:
            if (app.testPlaying) backToEditor();           // a test-play goes straight back to the editor
            else if (gameplayPaused()) resumeGameplay();
            else pauseGameplay();
            break;
        case Screen::Results: goToSongSelect(); break;
        case Screen::Tuner: leaveTuner(); break;
        case Screen::Instrument: leaveInstrument(); break;
        case Screen::Settings: if (!settingsUsedEscape()) leaveSettings(); break;
        case Screen::Calibration: leaveCalibration(); break;
        case Screen::Learn: if (learnBack()) app.screen = Screen::MainMenu; break;
        case Screen::EditorSelect: app.screen = Screen::MainMenu; break;
        case Screen::NewSong: if (!ImGui::GetIO().WantTextInput) goToSongList(Screen::EditorSelect); break;
        case Screen::Editor: break;       // the editors handle Esc themselves, to warn about unsaved changes
        case Screen::LessonEditor: break;
    }
}

// Shows the current menu screen and acts on the player's choice: this is the whole flow of the app
static void runMenus(){
    switch (app.screen){
        case Screen::MainMenu:
            switch (mainMenuScreen({app.settings.inputDevice, app.resourcesDir + "exercises", app.userExercisesDir, app.progressDir},
                                   app.mainMenuError)){
                case MainMenuChoice::Play: goToSongSelect(); break;
                case MainMenuChoice::Learn:
                    openLearnScreen({app.resourcesDir + "exercises", app.userExercisesDir, app.resourcesDir + "lessons", app.userLessonsDir,
                                     app.progressDir, app.settings});
                    app.screen = Screen::Learn;
                    break;
                case MainMenuChoice::Editor: goToSongList(Screen::EditorSelect); break;
                case MainMenuChoice::LessonEditor:
                    openLessonEditor({app.resourcesDir + "lessons", app.userLessonsDir, app.resourcesDir + "exercises", app.userExercisesDir});
                    app.screen = Screen::LessonEditor;
                    break;
                case MainMenuChoice::Tuner: goToTuner(); break;
                case MainMenuChoice::Instrument:
                    openInstrumentScreen(app.settings);
                    app.screen = Screen::Instrument;
                    break;
                case MainMenuChoice::Settings: goToSettings(); break;
                case MainMenuChoice::Quit: app.quit = true; break;
                case MainMenuChoice::None: break;
            }
            break;
        case Screen::SongSelect: {
            installDroppedPackages();
            SongSelectChoice choice = songSelectScreen("Select a song", app.songs, app.songSelectError, app.songSelectNotice, false,
                                                       &app.settings.playWithInstrument);
            if (choice.withInstrumentChanged) saveAppSettings();
            if (choice.back) app.screen = Screen::MainMenu;
            else if (choice.openDataFolder) openDataFolder();
            else if (choice.songIndex >= 0) startSong(app.songs[choice.songIndex], choice.part, choice.rhythmMode);
            break;
        }
        case Screen::EditorSelect: {
            installDroppedPackages();
            SongSelectChoice choice = songSelectScreen("Edit a song", app.songs, app.songSelectError, app.songSelectNotice, true);
            if (choice.back) app.screen = Screen::MainMenu;
            else if (choice.openDataFolder) openDataFolder();
            else if (choice.newSong){
                openNewSongScreen(app.userSongsDir);
                app.screen = Screen::NewSong;
            }
            else if (choice.songIndex >= 0) editSong(app.songs[choice.songIndex]);
            break;
        }
        case Screen::NewSong:
            switch (newSongScreen()){
                case NewSongChoice::Back: goToSongList(Screen::EditorSelect); break;
                case NewSongChoice::Created: {
                    // Straight into the editor with it; the list shows it next time
                    goToSongList(Screen::EditorSelect);
                    for (const SongEntry& song : app.songs) if (song.chartPath == newSongChartPath()) editSong(song);
                    break;
                }
                case NewSongChoice::None: break;
            }
            break;
        case Screen::Editor:
            switch (editorScreen()){
                case EditorChoice::Back:
                    closeEditor();
                    goToSongList(Screen::EditorSelect); // rescan: saving a built-in song created a new one
                    break;
                case EditorChoice::TestPlay: startTestPlay(); break;
                case EditorChoice::None: break;
            }
            break;
        case Screen::LessonEditor:
            if (lessonEditorScreen() == LessonEditorChoice::Back){
                closeLessonEditor();
                app.screen = Screen::MainMenu;
            }
            break;
        case Screen::Results:
            switch (resultsScreen(app.lastResult)){
                case ResultsChoice::Retry: startSong(app.currentSong, app.currentPart, app.currentRhythm); break;
                case ResultsChoice::BackToSongs: goToSongSelect(); break;
                case ResultsChoice::None: break;
            }
            break;
        case Screen::Tuner:
            if (tunerScreen()) leaveTuner();
            break;
        case Screen::Instrument: instrumentScreen(); break;
        case Screen::Settings:
            switch (settingsScreen(app.settings, app.soundsDir, app.settingsError)){
                case SettingsChoice::Back: leaveSettings(); break;
                case SettingsChoice::CalibrateTapping: goToCalibration(CalibrationMode::Tap); break;
                case SettingsChoice::CalibrateInstrument: goToCalibration(CalibrationMode::Instrument); break;
                case SettingsChoice::None: break;
            }
            break;
        case Screen::Calibration: {
            CalibrationChoice choice = calibrationScreen();
            if (choice.apply){
                if (app.calibrationMode == CalibrationMode::Tap) app.settings.globalOffsetMs = choice.offsetMs;
                else app.settings.inputOffsetMs = choice.offsetMs;
            }
            if (choice.apply || choice.back) leaveCalibration();
            break;
        }
        case Screen::Learn:
            if (learnScreen()){
                closeLearnScreen();
                app.screen = Screen::MainMenu;
            }
            break;
        case Screen::Playing: // the note views are drawn before the UI, with raylib
            if (!gameplayPaused()) drawGameplayHud(); // paused, the menu takes over the screen
            else {
                std::string song = app.currentSong.title;
                switch (pauseScreen(song)){
                    case PauseChoice::Resume: resumeGameplay(); break;
                    case PauseChoice::Retry:
                        if (app.testPlaying){ stopGameplay(); startTestPlay(); }
                        else startSong(app.currentSong, app.currentPart, app.currentRhythm);
                        break;
                    case PauseChoice::Quit:
                        if (app.testPlaying) backToEditor();
                        else { stopGameplay(); goToSongSelect(); }
                        break;
                    case PauseChoice::None: break;
                }
            }
            break;
    }
}

int main(void){
    const int INITIAL_WINDOW_WIDTH = 1280;
    const int INITIAL_WINDOW_HEIGHT = 720;

    // Resources are copied next to the executable at build time, so this works from any working directory
    app.resourcesDir = std::string(GetApplicationDirectory()) + "resources/";
    app.userDataDir = userDataDir();
    // The game was called OpenMusicTrainer: its data folder moves to the new name the first time
    std::string moveError;
    if (!moveUserDataFolder(oldUserDataDir(), app.userDataDir, moveError)) TraceLog(LOG_WARNING, "%s", moveError.c_str());
    app.userSongsDir = (fs::path(app.userDataDir) / "songs").string();
    app.soundsDir = (fs::path(app.userDataDir) / "sounds").string();
    app.settingsPath = (fs::path(app.userDataDir) / "settings.txt").string();
    app.progressDir = (fs::path(app.userDataDir) / "progress").string();
    app.userExercisesDir = (fs::path(app.userDataDir) / "exercises").string();
    app.userLessonsDir = (fs::path(app.userDataDir) / "lessons").string();
    app.packagesDir = (fs::path(app.userDataDir) / "packages").string();
    for (const std::string& dir : {app.userSongsDir, app.soundsDir, app.progressDir, app.userExercisesDir, app.userLessonsDir}){
        std::error_code ec;
        fs::create_directories(dir, ec);
        if (ec) TraceLog(LOG_WARNING, "Could not create %s: %s", dir.c_str(), ec.message().c_str());
    }
    TraceLog(LOG_INFO, "User data folder: %s", app.userDataDir.c_str());
    installCrashReport((fs::path(app.userDataDir) / "crash.txt").string());

    std::vector<std::string> warnings;
    app.settings = loadSettings(app.settingsPath, warnings);
    for (const std::string& warning : warnings) TraceLog(LOG_WARNING, "Settings: %s", warning.c_str());

    std::string error;
    if (!initAudio(app.settings.outputDevice, error)){
        TraceLog(LOG_ERROR, "Audio: %s", error.c_str());
        return 1;
    }
    TraceLog(LOG_INFO, "Audio: using %s backend, output '%s'", audioBackendName(), outputDeviceName());
    setMasterVolume(app.settings.masterVolume);
    setExclusiveCapture(app.settings.exclusiveInput);
    setHitSoundVolume(app.settings.hitSoundVolume);
    setPreviewVolume(app.settings.previewVolume);
    if (!setPreviewSound(app.settings.previewSound, app.soundsDir, error)){
        TraceLog(LOG_WARNING, "Preview sound: %s (using the drop)", error.c_str());
        app.settings.previewSound = "drop";
        setPreviewSound("drop", app.soundsDir, error);
    }

    // Multisampled: every edge the notes, rings and lines have is smoothed by the graphics card, where it would
    // otherwise step from pixel to pixel. A hint: a system without it just draws as before.
    SetConfigFlags(FLAG_MSAA_4X_HINT);
    InitWindow(INITIAL_WINDOW_WIDTH, INITIAL_WINDOW_HEIGHT, "lahn");
    SetExitKey(KEY_NULL); // Esc means "back" (handleBackKey), not "quit"
    applyDisplaySettings(app.settings);
    initUi(app.resourcesDir, app.settings.darkTheme);
    if (!loadStaffFont(app.resourcesDir + "fonts/Bravura.otf")) TraceLog(LOG_WARNING, "Music font not found: sheet music uses plain shapes");
    if (!loadViewFont(app.resourcesDir + "fonts/Figtree-Bold.ttf")) TraceLog(LOG_WARNING, "Text font not found: the note views use the pixel font");

    while (!WindowShouldClose() && !app.quit){
        const Screen shown = app.screen; // the screen this frame draws
        bool songOver = app.screen == Screen::Playing && !updateGameplay();
        if (app.screen == Screen::Tuner) updateTuner();

        BeginDrawing();
        if (app.screen == Screen::Playing) drawGameplay();
        else drawMenuBackground();

        beginUiFrame();
        runMenus();
        endUiFrame();

        // Changes of screen from outside the menus come after drawing, so this frame still shows the old screen and
        // the transition starts from it. Esc only counts if the menus didn't already use it to change screens.
        if (app.screen == shown) handleBackKey();
        if (songOver && app.screen == Screen::Playing && app.testPlaying) backToEditor();
        else if (songOver && app.screen == Screen::Playing){
            app.lastResult = gameplayResult();
            stopGameplay();
            recordRun(app.lastResult);
            const Rectangle& from = app.lastResult.distributionFrom;
            app.zoomTo = { from.x + from.width / 2, from.y + from.height / 2 }; // into the timing distribution
            app.screen = Screen::Results;
        }
        drawTransition();
        if (app.screen != shown){
            startTransition(app.zoomTo.x, app.zoomTo.y);
            app.zoomTo = {-1.0f, -1.0f};
        }
        EndDrawing();
    }

    stopGameplay();
    stopTuner();
    closeInstrumentScreen();
    closeEditor();
    closeLearnScreen(); // before the UI and audio they use shut down
    closeLessonEditor();
    stopCalibration();
    unloadTransition();
    closeUi();
    unloadStaffFont();
    unloadViewFont();
    CloseWindow();
    closeAudio();
    return 0;
}
