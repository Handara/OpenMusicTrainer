#include "raylib.h"
#include "app/crashreport.h"
#include "app/screenrecorder.h"
#include "app/videoconvert.h"
#include "audio/audio.h"
#include "core/judge.h"
#include "core/music.h"
#include "core/paths.h"
#include "core/routine.h"
#include "core/settings.h"
#include "core/songlibrary.h"
#include "core/songpackage.h"
#include "input/synthmonitor.h"
#include "screens/calibration.h"
#include "screens/editor.h"
#include "screens/gameplay.h"
#include "screens/importsong.h"
#include "screens/instrumentscreen.h"
#include "screens/learnscreen.h"
#include "screens/lessoneditor.h"
#include "screens/mainmenu.h"
#include "screens/menus.h"
#include "screens/newsong.h"
#include "screens/practicescreen.h"
#include "screens/settingsscreen.h"
#include "screens/tuner.h"
#include "screens/tuningscreen.h"
#include "input/menuinput.h"
#include "screens/tonewizard.h"
#include "ui/menulist.h"
#include "ui/transition.h"
#include "ui/ui.h"
#include "views/staff.h"
#include "views/viewfont.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

enum class Screen { MainMenu, SongSelect, Playing, Results, Tuner, Instrument, EditorSelect, NewSong, Editor, LessonEditor, Settings, Learn, Calibration,
                    TuningCheck, ToneWizard, ImportSong, Practice };

// App-wide state shared between screens
static struct App {
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
    bool currentPractice = false; // and whether it's being practised
    std::string songSelectError;  // why the last song failed to start (or a package failed to install)
    std::string songSelectNotice; // a song package just installed
    std::string packagesDir;      // song packages the player made, to share
    std::string mainMenuError;    // why the last main menu action failed (e.g. no input device)
    GameResult lastResult;
    Vector2 zoomTo = {-1.0f, -1.0f}; // the next change of screen zooms into this point (the end of a song)
    bool sameScreen = false;         // the next change of screen is a mode of the same one (the song list's): no transition
    bool testPlaying = false;     // playing the editor's chart: the end or Esc goes back to the editor
    CalibrationMode calibrationMode = CalibrationMode::Tap;
    std::string settingsError;    // why calibration couldn't start (e.g. no input device)
    bool importForEditor = false; // the import was opened from the editor's song list: what comes in opens in the editor
    InstrumentStatus guitar, bass; // whether each can be played now: looked at as a song's instruments are listed
    bool tuned[2] = {false, false}; // the guitar and the bass were checked in tune (or the player skipped it) this session
    SongEntry tuningFor;          // the song and part to go on with once the instrument is tuned...
    int tuningPart = 0;
    enum class AfterTuning { Play, ChoosePractice, Practise, Learn } afterTuning = AfterTuning::Play; // ...and how
    InputRole tuningRole = InputRole::Guitar; // the instrument checked, for Learn (it has no song)
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

static void checkInstruments();

static void goToLearn(){
    // The instrument played: the only one connected, or else the one played last
    checkInstruments();
    InputRole instrument = app.settings.heardInstrument;
    if (app.guitar.ready != app.bass.ready) instrument = app.guitar.ready ? InputRole::Guitar : InputRole::Bass;
    LearnSetup setup{ app.resourcesDir + "exercises", app.userExercisesDir, app.resourcesDir + "lessons", app.userLessonsDir,
                      app.progressDir, app.settings };
    setup.instrument = instrument;
    setup.piano = app.settings.learnOnPiano;
    openLearnScreen(setup);
    app.screen = Screen::Learn;
}

// Where add-ons are installed (the stems add-on, the video add-on)
static std::string addonsDir(){
    return (fs::path(app.userDataDir) / "addons").string();
}

static void goToImport(const std::string& file){
    app.importForEditor = app.screen == Screen::EditorSelect;
    openImportScreen(app.userSongsDir, addonsDir(), file);
    app.screen = Screen::ImportSong;
}

// What the import screen takes: a Guitar Pro tab, a song or a recording of a bass to write down, the stems add-on
static bool isImportable(const std::string& path){
    std::string extension = fs::path(path).extension().string();
    for (char& c : extension) c = (char)std::tolower((unsigned char)c);
    for (const char* kind : { ".gp", ".gpx", ".gp5", ".gp4", ".gp3", ".mp3", ".ogg", ".flac", ".wav", ".lahnaddon" }) if (extension == kind) return true;
    return false;
}

static bool isAudioFile(const std::string& path){
    std::string extension = fs::path(path).extension().string();
    for (char& c : extension) c = (char)std::tolower((unsigned char)c);
    return extension == ".mp3" || extension == ".ogg" || extension == ".flac" || extension == ".wav";
}

// Song packages (.lahn) dropped on a song list are installed into the player's songs, and the list shows them. On the
// list to play, a Guitar Pro tab or a recording goes to the import screen; on the editor's list, an audio file makes a
// new song of it; on either, a video does (its sound the song's audio, its pictures behind the notes).
static void installDroppedPackages(){
    if (!IsFileDropped()) return;
    FilePathList dropped = LoadDroppedFiles();
    for (unsigned i = 0; i < dropped.count; i++){
        std::string path = dropped.paths[i];
        bool editing = app.screen == Screen::EditorSelect;
        if (!isImportable(path) && !isVideoFile(path)) continue;
        UnloadDroppedFiles(dropped);
        if ((editing && isAudioFile(path)) || isVideoFile(path)){
            openNewSongScreen(app.userSongsDir, addonsDir(), path);
            app.screen = Screen::NewSong;
        } else {
            goToImport(path);
        }
        return;
    }
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

// A song deleted from a list: into the trash folder of the data folder, and the list read again
static void deleteSong(int index){
    if (index < 0 || index >= (int)app.songs.size() || app.songs[index].builtIn) return;
    std::string title = app.songs[index].title, error;
    bool gone = trashSong(app.songs[index].folder, (fs::path(app.userDataDir) / "trash").string(), error);
    goToSongList(app.screen);
    app.songSelectError = gone ? "" : error;
    app.songSelectNotice = gone ? "Deleted: " + title + " (it's in the trash folder of your data folder)" : "";
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

// Hearing the instrument, on or off from anywhere: F2, or the pill at the top right of the menus and the pause menu
static void toggleHearing(){
    app.settings.monitorOn = !app.settings.monitorOn;
    std::string error;
    applyMonitor(app.settings, error);
    if (!error.empty()) TraceLog(LOG_WARNING, "Hearing the instrument: %s", error.c_str());
    saveAppSettings();
}
static bool hearingButton(float s){
    const char* instrument = app.settings.heardInstrument == InputRole::Guitar ? "guitar" : "bass";
    const std::string text = TextFormat("Hear my %s: %s", instrument, app.settings.monitorOn ? "on" : "off");
    return menuPill(text.c_str(), "F2", ImVec2(ImGui::GetIO().DisplaySize.x * 0.93f, 21 * s), true, 0, s);
}

static void leaveSettings(){
    closeSettingsScreen();
    std::string monitorError;
    applyMonitor(app.settings, monitorError); // its inputs and amp as the settings now say
    saveAppSettings();
    checkInstruments(); // the inputs may have changed: which instruments can steer the menus
    app.screen = Screen::MainMenu;
}

static void editSong(const SongEntry& song){
    std::string error;
    if (openEditor(song, app.userSongsDir, app.packagesDir, addonsDir(), app.settings, error)){
        app.songSelectError.clear();
        app.screen = Screen::Editor;
    } else {
        app.songSelectError = error;
    }
}

// A finished run goes into its part's records (not a test-play from the editor: that isn't a real run). The
// result then knows where it placed, and shows the part's best runs.
// What happened to every note of the last run, in the data folder (last-run.txt): each note's timing, and for a note
// missed, what was heard nearest it. To see why notes go unplayed: heard as another pitch, heard too far off, or not
// heard at all.
static void writeRunLog(const GameResult& result){
    std::ofstream out(fs::path(app.userDataDir) / "last-run.txt");
    if (!out) return;
    out << "# " << result.title << " (" << result.partName << ")\n";
    out << "# perfect within " << (int)std::lround(PERFECT_WINDOW_S * 1000) << " ms, good within " << (int)std::lround(NEAR_WINDOW_S * 1000)
        << " ms; input offset " << app.settings.inputOffsetMs << " ms\n";
    out << TextFormat("# %d perfect, %d good, %d missed; timing: mean %+.1f ms (+ early, - late), unstable rate %.0f\n",
                      result.perfectCount, result.nearCount, result.missCount, result.timing.meanMs, result.timing.unstableRate);
    out << "# time      note   result    error / heard nearest\n";
    auto name = [](int pitch){ return std::string(TextFormat("%s%d", pitchClassName(pitch), pitchOctave(pitch))); };
    for (const WrittenNote& note : result.written){
        out << TextFormat("%8.3f  %-5s  ", note.time, name(note.pitch).c_str());
        if (note.hit){
            out << TextFormat("%-8s  %+.0f ms\n", note.perfect ? "perfect" : "good", note.errorMs);
            continue;
        }
        const HeardPitch* nearest = nullptr;
        for (const HeardPitch& heard : result.heard){
            if (std::fabs(heard.time - note.time) < 0.4f && (!nearest || std::fabs(heard.time - note.time) < std::fabs(nearest->time - note.time))) nearest = &heard;
        }
        if (!nearest) out << "MISSED    nothing heard within 400 ms\n";
        else out << TextFormat("MISSED    heard %s at %+.0f ms%s\n", name(nearest->pitch).c_str(), (note.time - nearest->time) * 1000.0f,
                               nearest->pitch == note.pitch ? " (the right pitch: too far off)" :
                               (nearest->pitch - note.pitch) % 12 == 0 ? " (another octave)" : "");
    }
}

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
    std::vector<RunRecord> history = loadHistory(historyPath(path), records); // before this run: records may start it
    result.place = addRun(records, run);
    std::string error;
    if (result.place >= 0 && !saveRuns(path, records, error)) TraceLog(LOG_WARNING, "Records: %s", error.c_str());
    if (!addToHistory(historyPath(path), history, run, error)) TraceLog(LOG_WARNING, "Records: %s", error.c_str());
    result.records = records;
    result.history = history;
}

static GameplayOptions gameplayOptions(){
    GameplayOptions options;
    options.noteSpeed = app.settings.noteSpeed;
    options.offsetSeconds = app.settings.globalOffsetMs / 1000.0f;
    options.lowStringOnTop = app.settings.lowStringOnTop;
    options.video = app.settings.songVideo;
    options.noteViews = app.settings.noteViews;
    options.playWithInstrument = false; // set by startSong, from the part: each is played on its own instrument
    options.inputDevice = app.settings.inputDevice;
    options.guitarChannel = app.settings.guitarChannel;
    options.bassChannel = app.settings.bassChannel;
    options.midiDevice = app.settings.midiDevice;
    options.pianoKeys = app.settings.pianoKeys;
    options.inputOffsetSeconds = app.settings.inputOffsetMs / 1000.0f;
    options.hitSounds = true;
    options.hitSoundIsNote = app.settings.hitSoundIsNote;
    options.hitSoundVolume = app.settings.hitSoundVolume;
    options.keyVolume = app.settings.previewVolume;
    return options;
}

// The instrument a part is played on: its own (keys have none here: a MIDI keyboard or the computer's)
static bool partInstrument(const SongEntry& song, int part, InputRole& role){
    if (part < 0 || part >= (int)song.parts.size() || song.parts[part].type == InstrumentType::Keys) return false;
    role = song.parts[part].type == InstrumentType::Bass ? InputRole::Bass : InputRole::Guitar;
    return true;
}

static InstrumentStatus& statusOf(InputRole role){
    return role == InputRole::Bass ? app.bass : app.guitar;
}

// Whether the guitar and the bass can be played now: the input device is there (a device chosen by name, not the
// system default the audio falls back to without it), it opens, and it has the input each is set to. Asked as a
// song's instruments are listed: looking for devices is too slow for every frame.
static void checkInstruments(){
    std::string error;
    std::vector<std::string> devices = app.settings.inputDevice.empty() ? std::vector<std::string>{} : inputDeviceNames();
    bool present = app.settings.inputDevice.empty()
                || std::find(devices.begin(), devices.end(), app.settings.inputDevice) != devices.end();
    bool open = present && startCapture(app.settings.inputDevice, error);
    int channels = open ? captureChannels() : 0;
    if (open) stopCapture();
    std::string device = app.settings.inputDevice.empty() ? "your audio interface" : app.settings.inputDevice;
    for (InputRole role : { InputRole::Guitar, InputRole::Bass }){
        InstrumentStatus& status = statusOf(role);
        int channel = role == InputRole::Bass ? app.settings.bassChannel : app.settings.guitarChannel;
        const char* name = role == InputRole::Bass ? "bass" : "guitar";
        status.ready = open && channel < channels;
        if (!open) status.problem = TextFormat("Connect %s to play this on your %s", device.c_str(), name);
        else if (!status.ready) status.problem = TextFormat("Your %s is set to input %d, which isn't there: see Settings, Instruments", name, channel + 1);
        else status.problem.clear();
    }
}

// How a song's part is played: on its own instrument when it's connected (heard through its tone)
static GameplayOptions songOptions(const SongEntry& song, int part){
    GameplayOptions options = gameplayOptions();
    options.part = part;
    InputRole role;
    if (partInstrument(song, part, role) && statusOf(role).ready){
        hearInstrument(app.settings, role); // through its own tone
        options.playWithInstrument = true;
        options.instrument = role;
        options.hitSounds = false;
    }
    return options;
}

static void startSong(const SongEntry& song, int part){
    std::string error;
    app.testPlaying = false;
    if (startGameplay(song.chartPath, songOptions(song, part), error)){
        app.currentPractice = false;
        app.currentSong = song;
        app.currentPart = part;
        app.songSelectError.clear();
        app.screen = Screen::Playing;
    } else {
        app.songSelectError = error;
        app.screen = Screen::SongSelect;
    }
}

// The practice screen for a part; `around`: the place in the song to choose bars around (-1: the last ones)
static void goToPractice(const SongEntry& song, int part, double around, const std::string& message){
    std::string error;
    if (!openPracticeScreen(song, part, around, message, error)){
        app.songSelectError = error;
        goToSongSelect();
        return;
    }
    app.currentSong = song;
    app.currentPart = part;
    app.screen = Screen::Practice;
}

// The practice chosen on its screen, started
static void startPractice(){
    std::string error;
    app.testPlaying = false;
    GameplayOptions options = songOptions(app.currentSong, app.currentPart);
    options.practice = practiceChoice();
    if (startGameplay(app.currentSong.chartPath, options, error)){
        app.currentPractice = true;
        app.screen = Screen::Playing;
    } else {
        goToPractice(app.currentSong, app.currentPart, -1, "Couldn't start: " + error);
    }
}

// A practice over: back on its screen, with how it went
static void endPractice(){
    PracticeProgress progress = practiceProgress();
    stopGameplay();
    std::string message;
    if (progress.mastered) message = TextFormat("Mastered at %d%% tempo, after %d %s.", (int)std::lround(progress.speed * 100.0f), progress.passes,
                                                progress.passes == 1 ? "pass" : "passes");
    else message = TextFormat("%d %s, the best with %d%% of the notes, at up to %d%% tempo.", progress.passes, progress.passes == 1 ? "pass" : "passes",
                              (int)std::lround(progress.bestAccuracy * 100.0f), (int)std::lround(progress.speed * 100.0f));
    goToPractice(app.currentSong, app.currentPart, -1, message);
}

// The tuning check for a part's instrument; `reason` says why when it isn't the first time (it went out of tune)
static bool goToTuningCheck(const SongEntry& song, int part, App::AfterTuning after, const std::string& reason){
    InputRole role;
    if (!partInstrument(song, part, role)) return false;
    std::string error;
    int channel = role == InputRole::Bass ? app.settings.bassChannel : app.settings.guitarChannel;
    hearInstrument(app.settings, role);
    if (!openTuningScreen(song.parts[part].tuning, role, app.settings.inputDevice, channel, reason, error)){
        TraceLog(LOG_WARNING, "Tuning check: %s", error.c_str());
        return false;
    }
    app.tuningFor = song;
    app.tuningPart = part;
    app.afterTuning = after;
    app.screen = Screen::TuningCheck;
    return true;
}

// A part chosen on the song list: the first time an instrument is played in a session, it's checked in tune first
static void chooseSong(const SongEntry& song, int part, bool practice){
    InputRole role;
    App::AfterTuning after = practice ? App::AfterTuning::ChoosePractice : App::AfterTuning::Play;
    if (partInstrument(song, part, role) && statusOf(role).ready && !app.tuned[(int)role]
        && goToTuningCheck(song, part, after, "")) return;
    if (practice) goToPractice(song, part, -1, "");
    else startSong(song, part);
}

// Before something in Learn played on the instrument: its standard tuning checked, the first time this session
static bool goToLearnTuningCheck(InputRole role){
    const std::vector<int> tuning = role == InputRole::Bass ? std::vector<int>{ 28, 33, 38, 43 } : std::vector<int>{ 40, 45, 50, 55, 59, 64 };
    const int channel = role == InputRole::Bass ? app.settings.bassChannel : app.settings.guitarChannel;
    std::string error;
    if (!openTuningScreen(tuning, role, app.settings.inputDevice, channel, "", error)){
        TraceLog(LOG_WARNING, "Tuning check: %s", error.c_str());
        return false;
    }
    app.afterTuning = App::AfterTuning::Learn;
    app.tuningRole = role;
    app.screen = Screen::TuningCheck;
    return true;
}

static void leaveTuningCheck(bool tuned){
    closeTuningScreen();
    if (app.afterTuning == App::AfterTuning::Learn){
        if (tuned) app.tuned[(int)app.tuningRole] = true;
        app.screen = Screen::Learn;
        learnTuningDone(tuned);
        return;
    }
    InputRole role;
    if (!tuned){
        app.screen = Screen::SongSelect;
        return;
    }
    if (partInstrument(app.tuningFor, app.tuningPart, role)) app.tuned[(int)role] = true;
    switch (app.afterTuning){
        case App::AfterTuning::Play: startSong(app.tuningFor, app.tuningPart); break;
        case App::AfterTuning::ChoosePractice: goToPractice(app.tuningFor, app.tuningPart, -1, ""); break;
        case App::AfterTuning::Practise: app.currentSong = app.tuningFor; app.currentPart = app.tuningPart; startPractice(); break;
        case App::AfterTuning::Learn: break; // handled above
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

// The screens with a way back show the Back button at their top left: all but the main menu, the play screen (Esc
// pauses it, and the pause menu has Resume) and the editors (they warn about unsaved changes first)
static bool hasBackButton(Screen screen){
    switch (screen){
        case Screen::MainMenu: case Screen::Playing: case Screen::Editor: case Screen::LessonEditor: return false;
        default: return true;
    }
}

// Esc always means "back", and so do the Back button and the mouse's back button. Handled in one place so a single
// press can't trigger two transitions in one frame.
static void handleBackKey(bool backClicked){
    bool mouseBack = hasBackButton(app.screen) && (IsMouseButtonPressed(MOUSE_BUTTON_SIDE) || IsMouseButtonPressed(MOUSE_BUTTON_BACK));
    if (!IsKeyPressed(KEY_ESCAPE) && !backClicked && !mouseBack && !menuInputBack()) return;
    switch (app.screen){
        case Screen::MainMenu: break;
        case Screen::SongSelect: if (!songSelectBack()) app.screen = Screen::MainMenu; break;
        case Screen::Playing:
            if (app.testPlaying) backToEditor();           // a test-play goes straight back to the editor
            else if (gameplayPaused()) resumeGameplay();
            else pauseGameplay();
            break;
        case Screen::Results: if (!resultsBack()) goToSongSelect(); break;
        case Screen::Tuner: leaveTuner(); break;
        case Screen::Instrument: leaveInstrument(); break;
        case Screen::Settings: if (!settingsUsedEscape()) leaveSettings(); break;
        case Screen::Calibration: leaveCalibration(); break;
        case Screen::TuningCheck: leaveTuningCheck(false); break;
        case Screen::Practice: closePracticeScreen(); goToSongSelect(); break;
        case Screen::ImportSong:
            closeImportScreen();
            goToSongList(app.importForEditor ? Screen::EditorSelect : Screen::SongSelect); // back where it was opened from
            break;
        case Screen::ToneWizard:
            if (!ImGui::GetIO().WantTextInput){ // Esc in the name field stops typing, it doesn't leave
                closeToneWizard(app.settings);
                saveAppSettings();
                app.screen = Screen::Settings;
            }
            break;
        case Screen::Learn:
            if (learnBack()){
                closeLearnScreen();
                app.screen = Screen::MainMenu;
            }
            break;
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
                case MainMenuChoice::Learn: goToLearn(); break;
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
                                                       app.guitar, app.bass);
            if (choice.partsOpened) checkInstruments();
            if (choice.importSong) goToImport("");
            else if (choice.deleteSong >= 0) deleteSong(choice.deleteSong);
            else if (choice.back) app.screen = Screen::MainMenu;
            else if (choice.openDataFolder) openDataFolder();
            else if (choice.switchEditing){ app.screen = Screen::EditorSelect; app.sameScreen = true; }
            else if (choice.songIndex >= 0) chooseSong(app.songs[choice.songIndex], choice.part, choice.practice);
            break;
        }
        case Screen::EditorSelect: {
            installDroppedPackages();
            SongSelectChoice choice = songSelectScreen("Edit a song", app.songs, app.songSelectError, app.songSelectNotice, true);
            if (choice.back) app.screen = Screen::MainMenu;
            else if (choice.switchEditing){ app.screen = Screen::SongSelect; app.sameScreen = true; }
            else if (choice.importSong) goToImport("");
            else if (choice.deleteSong >= 0) deleteSong(choice.deleteSong);
            else if (choice.openDataFolder) openDataFolder();
            else if (choice.newSong){
                openNewSongScreen(app.userSongsDir, addonsDir());
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
                    // Made from a video: its pictures are brought in now, the editor showing how far along
                    if (app.screen == Screen::Editor && !newSongVideoPath().empty()) editorImportFile(newSongVideoPath());
                    // Its tempo and bars, found from the song itself (after the video, if there's one)
                    if (app.screen == Screen::Editor && newSongFindsTempo()) editorFindTempo();
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
                goToLearn(); // where it's opened from: rescanned, so a lesson just made is there
            }
            break;
        case Screen::Results:
            switch (resultsScreen(app.lastResult)){
                case ResultsChoice::Retry: startSong(app.currentSong, app.currentPart); break;
                case ResultsChoice::BackToSongs: goToSongSelect(); break;
                case ResultsChoice::None: break;
            }
            break;
        case Screen::Tuner:
            tunerScreen();
            break;
        case Screen::Instrument: instrumentScreen(); break;
        case Screen::Settings:
            switch (settingsScreen(app.settings, app.soundsDir, app.settingsError)){
                case SettingsChoice::Back: leaveSettings(); break;
                case SettingsChoice::CalibrateTapping: goToCalibration(CalibrationMode::Tap); break;
                case SettingsChoice::CalibrateInstrument: goToCalibration(CalibrationMode::Instrument); break;
                case SettingsChoice::ToneWizard:
                    openToneWizard(app.settings);
                    app.screen = Screen::ToneWizard;
                    break;
                case SettingsChoice::None: break;
            }
            break;
        case Screen::Calibration: {
            CalibrationChoice choice = calibrationScreen();
            if (choice.apply){
                if (app.calibrationMode == CalibrationMode::Tap) app.settings.globalOffsetMs = choice.offsetMs;
                else app.settings.inputOffsetMs = choice.offsetMs;
            }
            if (choice.apply) leaveCalibration();
            break;
        }
        case Screen::ToneWizard: toneWizardScreen(app.settings); break;
        case Screen::ImportSong:
            if (importScreen() == ImportChoice::Imported){
                std::string title = importedSongTitle();
                closeImportScreen();
                goToSongList(app.importForEditor ? Screen::EditorSelect : Screen::SongSelect); // rescanned: it's there
                app.songSelectNotice = "Added: " + title;
                for (int i = 0; i < (int)app.songs.size(); i++){
                    if (app.songs[i].title != title || app.songs[i].builtIn) continue;
                    if (app.importForEditor) editSong(app.songs[i]); // from the editor's list: straight into the editor
                    else selectSongInList(i);
                    break;
                }
            }
            break;
        case Screen::Practice:
            if (practiceScreen() == PracticeChoice::Start) startPractice();
            break;
        case Screen::TuningCheck:
            switch (tuningScreen()){
                case TuningChoice::Tuned: case TuningChoice::Skipped: leaveTuningCheck(true); break;
                case TuningChoice::None: break;
            }
            break;
        case Screen::Learn: {
            learnScreen();
            // The instrument switched: kept as the one played now (and heard through its tone)
            InputRole played;
            bool piano = false;
            if (learnChangedInstrument(played, piano)){
                app.settings.learnOnPiano = piano;
                if (!piano) hearInstrument(app.settings, played);
                saveAppSettings();
            }
            // Something played on it about to start: in tune first, once a session, as before a song
            if (learnWantsTuning(played)){
                if (!(statusOf(played).ready && !app.tuned[(int)played] && goToLearnTuningCheck(played))) learnTuningDone(true);
            }
            if (learnWantsEditor()){
                closeLearnScreen();
                openLessonEditor({app.resourcesDir + "lessons", app.userLessonsDir, app.resourcesDir + "exercises", app.userExercisesDir});
                app.screen = Screen::LessonEditor;
            }
            break;
        }
        case Screen::Playing: // the note views are drawn before the UI, with raylib
            float cents;
            if (!gameplayPaused()) drawGameplayHud(); // paused, the menu takes over the screen
            else if (gameplayOutOfTune(cents)){
                InputRole role = InputRole::Guitar;
                partInstrument(app.currentSong, app.currentPart, role);
                switch (outOfTuneScreen(app.currentSong.title, role == InputRole::Bass ? "bass" : "guitar", cents)){
                    case OutOfTuneChoice::Retune: {
                        stopGameplay();
                        std::string reason = TextFormat("It sounded about %.0f cents %s. Once it's in tune, the song starts over.",
                                                        std::fabs(cents), cents > 0 ? "sharp" : "flat");
                        App::AfterTuning after = app.currentPractice ? App::AfterTuning::Practise : App::AfterTuning::Play;
                        if (!goToTuningCheck(app.currentSong, app.currentPart, after, reason)) goToSongSelect();
                        break;
                    }
                    case OutOfTuneChoice::PlayOn: resumeGameplay(); break;
                    case OutOfTuneChoice::Quit: stopGameplay(); goToSongSelect(); break;
                    case OutOfTuneChoice::None: break;
                }
            } else {
                std::string song = app.currentSong.title;
                InputRole role;
                const bool tunable = !app.testPlaying && partInstrument(app.currentSong, app.currentPart, role) && statusOf(role).ready;
                const char* instrument = tunable ? (role == InputRole::Bass ? "bass" : "guitar") : nullptr;
                switch (pauseScreen(song, app.currentPractice, instrument, !app.testPlaying)){
                    case PauseChoice::Resume: resumeGameplay(); break;
                    case PauseChoice::Retry:
                        if (app.testPlaying){ stopGameplay(); startTestPlay(); }
                        else if (app.currentPractice){ stopGameplay(); startPractice(); }
                        else startSong(app.currentSong, app.currentPart);
                        break;
                    case PauseChoice::PracticeSettings: stopGameplay(); goToPractice(app.currentSong, app.currentPart, -1, ""); break;
                    case PauseChoice::SwitchMode:
                        if (app.currentPractice){ stopGameplay(); startSong(app.currentSong, app.currentPart); }
                        else {
                            double around = gameplaySongTime();
                            stopGameplay();
                            goToPractice(app.currentSong, app.currentPart, around, "");
                        }
                        break;
                    case PauseChoice::Tune: {
                        stopGameplay();
                        App::AfterTuning after = app.currentPractice ? App::AfterTuning::Practise : App::AfterTuning::Play;
                        std::string reason = app.currentPractice ? "Once it's in tune, the practice starts again." : "Once it's in tune, the song starts over.";
                        if (!goToTuningCheck(app.currentSong, app.currentPart, after, reason)) goToSongSelect();
                        break;
                    }
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
    initTones((fs::path(app.userDataDir) / "tones").string());
    std::string monitorError;
    applyMonitor(app.settings, monitorError); // the instrument heard from the start
    checkInstruments(); // which instruments are connected: they can steer the menus
    if (!monitorError.empty()) TraceLog(LOG_WARNING, "Hearing the instrument: %s", monitorError.c_str());
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
        updateSynthMonitor(app.settings.monitorOn && app.settings.monitorSynth, app.settings.monitorVolume); // heard wherever the player is
        bool songOver = app.screen == Screen::Playing && !updateGameplay();
        if (app.screen == Screen::Tuner) updateTuner();

        // The instrument steers the menus (its open strings as arrows), on the screens that are menus
        const bool menuScreen = app.screen == Screen::MainMenu || app.screen == Screen::SongSelect || (app.screen == Screen::Learn && learnInMenus());
        const InstrumentStatus& played = statusOf(app.settings.heardInstrument);
        updateMenuInput(menuScreen, played.ready ? "" : played.problem.empty() ? "no instrument connected" : played.problem, app.settings,
                        app.settings.heardInstrument);

        BeginDrawing();
        if (app.screen == Screen::Playing) drawGameplay();
        else drawMenuBackground();

        beginUiFrame();
        runMenus();
        bool backClicked = app.screen == shown && hasBackButton(shown) && menuBackButton(menuScale());
        const bool hearingShown = hasBackButton(shown) || (shown == Screen::Playing && gameplayPaused());
        if ((app.screen == shown && hearingShown && hearingButton(menuScale())) || IsKeyPressed(KEY_F2)) toggleHearing();
        if (app.screen == shown && menuScreen) drawMenuInputLegend(menuScale());
        endUiFrame();

        // Changes of screen from outside the menus come after drawing, so this frame still shows the old screen and
        // the transition starts from it. Esc only counts if the menus didn't already use it to change screens.
        if (app.screen == shown) handleBackKey(backClicked);
        if (songOver && app.screen == Screen::Playing && app.testPlaying) backToEditor();
        else if (songOver && app.screen == Screen::Playing && app.currentPractice) endPractice();
        else if (songOver && app.screen == Screen::Playing){
            app.lastResult = gameplayResult();
            stopGameplay();
            recordRun(app.lastResult);
            writeRunLog(app.lastResult);
            const Rectangle& from = app.lastResult.distributionFrom;
            app.zoomTo = { from.x + from.width / 2, from.y + from.height / 2 }; // into the timing distribution
            app.screen = Screen::Results;
        }
        drawTransition();
        captureScreen(); // a check's video, recording (F9 in a song): the frame as it will be shown
        if (app.screen != shown && app.sameScreen) app.sameScreen = false;
        else if (app.screen != shown){
            startTransition(app.zoomTo.x, app.zoomTo.y);
            app.zoomTo = {-1.0f, -1.0f};
        }
        EndDrawing();
    }

    saveAppSettings(); // what changed outside the settings screen too (the instrument played last)
    stopGameplay();
    waitForChecks();
    stopTuner();
    closeInstrumentScreen();
    closeEditor();
    closeLearnScreen(); // before the UI and audio they use shut down
    closeLessonEditor();
    stopCalibration();
    closeTuningScreen();
    closeImportScreen(); // its listening thread stopped
    unloadTransition();
    closeUi();
    unloadStaffFont();
    unloadViewFont();
    CloseWindow();
    closeAudio();
    return 0;
}
