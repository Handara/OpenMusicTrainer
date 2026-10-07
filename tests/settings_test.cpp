#include "doctest/doctest.h"

#include "core/settings.h"

#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

static std::string settingsPath(const std::string& name){
    fs::path dir = fs::temp_directory_path() / "lahn_tests";
    fs::create_directories(dir);
    return (dir / name).string();
}

TEST_CASE("a missing settings file gives defaults without warnings"){
    std::vector<std::string> warnings;
    Settings settings = loadSettings(settingsPath("does_not_exist.txt"), warnings);
    CHECK(warnings.empty());
    CHECK(settings.previewSound == "drop");
    CHECK(settings.frameRateLimit == 60);
    CHECK(settings.outputDevice.empty());
}

TEST_CASE("settings survive a save and load"){
    Settings original;
    original.outputDevice = "Speakers (Realtek(R) Audio)"; // spaces and brackets, like real device names
    original.inputDevice = "Focusrite USB";
    original.masterVolume = 0.8f;
    original.previewVolume = 0.25f;
    original.editorNoteVolume = 0.45f;
    original.editorSongVolume = 0.35f;
    original.editorNoteNames = false;
    original.previewSound = "my rain.wav";
    original.fullscreen = true;
    original.darkTheme = false; // dark is the default: light must come back as light
    original.hitSoundIsNote = false;
    original.lowStringOnTop = false;
    original.noteViews.staff = true; // with the neck
    original.frameRateLimit = 144;
    original.noteSpeed = 450.0f;
    original.globalOffsetMs = -35;
    original.inputOffsetMs = 22;
    original.playWithInstrument = true;
    original.playInstrument = InputRole::Bass;

    std::string path = settingsPath("settings.txt");
    std::string error;
    REQUIRE_MESSAGE(saveSettings(path, original, error), error);
    std::vector<std::string> warnings;
    Settings loaded = loadSettings(path, warnings);
    CHECK(warnings.empty());
    CHECK(loaded.outputDevice == original.outputDevice);
    CHECK(loaded.inputDevice == original.inputDevice);
    CHECK(loaded.masterVolume == doctest::Approx(0.8f));
    CHECK(loaded.previewVolume == doctest::Approx(0.25f));
    CHECK(loaded.editorNoteVolume == doctest::Approx(0.45f));
    CHECK(loaded.editorSongVolume == doctest::Approx(0.35f));
    CHECK_FALSE(loaded.editorNoteNames);
    CHECK(loaded.previewSound == "my rain.wav");
    CHECK(loaded.fullscreen);
    CHECK_FALSE(loaded.darkTheme);
    CHECK_FALSE(loaded.hitSoundIsNote);
    CHECK_FALSE(loaded.lowStringOnTop);
    CHECK(loaded.noteViews.staff);
    CHECK(loaded.noteViews.neck);
    CHECK(loaded.frameRateLimit == 144);
    CHECK(loaded.noteSpeed == doctest::Approx(450.0f));
    CHECK(loaded.globalOffsetMs == -35);
    CHECK(loaded.inputOffsetMs == 22);
    CHECK(loaded.playWithInstrument);
    CHECK(loaded.playInstrument == InputRole::Bass);
}

TEST_CASE("bad lines are reported but don't lose the rest"){
    std::string path = settingsPath("messy.txt");
    std::ofstream(path, std::ios::binary) << "version 1\r\n"
                                              "frame_rate_limit fast\r\n"      // not a number
                                              "note_speed 99999\r\n"           // out of range
                                              "some_future_setting 3\r\n"      // from a newer version
                                              "global_offset_ms 20\r\n";       // fine
    std::vector<std::string> warnings;
    Settings settings = loadSettings(path, warnings);
    CHECK(warnings.size() == 2);
    CHECK(settings.frameRateLimit == 60);              // kept the default
    CHECK(settings.noteSpeed == doctest::Approx(1500.0f)); // clamped to the maximum
    CHECK(settings.globalOffsetMs == 20);              // still loaded after the problems
}

TEST_CASE("note views: sheet music, the neck or both, and the older forms"){
    struct Case { const char* line; bool staff, neck; size_t warnings; };
    const Case cases[] = {
        {"note_view staff",          true,  false, 0},
        {"note_view neck",           false, true,  0},
        {"note_view staff neck",     true,  true,  0},
        {"note_view highway staff",  true,  true,  0}, // the highway and the tab are gone: the neck shows the notes now
        {"note_view tab",            false, true,  0},
        {"note_view both",           true,  true,  0}, // how files first wrote sheet music and the highway
        {"note_view",                false, true,  1}, // nothing on: keep the default
        {"note_view staff piano",    false, true,  1}, // an unknown word: keep the default, not half of it
    };
    for (const Case& c : cases){
        CAPTURE(c.line);
        std::string path = settingsPath("views.txt");
        std::ofstream(path, std::ios::binary) << "version 1\nhighway_direction falling\n" << c.line << "\n";
        std::vector<std::string> warnings;
        Settings settings = loadSettings(path, warnings);
        CHECK(settings.noteViews.staff == c.staff);
        CHECK(settings.noteViews.neck == c.neck);
        CHECK(warnings.size() == c.warnings); // an old highway_direction line is no trouble
    }
    CHECK(Settings{}.noteViews.neck);
    CHECK_FALSE(Settings{}.noteViews.staff);
}

TEST_CASE("notes say their fret, their name or both: both unless the player chooses"){
    CHECK(Settings{}.noteViews.label == NoteLabel::Both);
    for (NoteLabel label : { NoteLabel::Fret, NoteLabel::Name, NoteLabel::Both }){
        std::string path = settingsPath("label.txt");
        Settings settings;
        settings.noteViews.label = label;
        std::string error;
        REQUIRE(saveSettings(path, settings, error));
        std::vector<std::string> warnings;
        CHECK(loadSettings(path, warnings).noteViews.label == label);
        CHECK(warnings.empty());
    }
}

TEST_CASE("the neck: whole unless the player keeps it to the song's frets"){
    CHECK(Settings{}.noteViews.wholeNeck);
    std::string path = settingsPath("neckrange.txt");
    std::ofstream(path, std::ios::binary) << "version 1\nneck_range song\nneck_style flat\nnote_view neck\n";
    std::vector<std::string> warnings;
    Settings settings = loadSettings(path, warnings);
    CHECK(warnings.empty());
    CHECK_FALSE(settings.noteViews.wholeNeck); // reading note_view after them doesn't reset them
    std::string error;
    REQUIRE(saveSettings(path, settings, error));
    Settings again = loadSettings(path, warnings);
    CHECK_FALSE(again.noteViews.wholeNeck);
}

TEST_CASE("each instrument's input, as the interface numbers them"){
    Settings settings;
    CHECK(settings.guitarChannel == -1);            // all inputs mixed until told otherwise
    settings.guitarChannel = 0;                     // input 1
    settings.bassChannel = 1;                       // input 2
    std::filesystem::path path = std::filesystem::temp_directory_path() / "lahn_tests" / "inputs_settings.txt";
    std::filesystem::create_directories(path.parent_path());
    std::string error;
    REQUIRE_MESSAGE(saveSettings(path.string(), settings, error), error);
    std::vector<std::string> warnings;
    Settings loaded = loadSettings(path.string(), warnings);
    CHECK(warnings.empty());
    CHECK(loaded.guitarChannel == 0);
    CHECK(loaded.bassChannel == 1);
    CHECK(loaded.voiceChannel == -1);
    CHECK(channelFor(loaded, InputRole::Bass) == 1);
}

TEST_CASE("the input is kept to lahn alone unless the player shares it"){
    Settings settings;
    CHECK(settings.exclusiveInput); // Windows' effects on microphones cut instruments: skipped by default
    settings.exclusiveInput = false; // to use the mic in a voice chat at the same time
    std::filesystem::path path = std::filesystem::temp_directory_path() / "lahn_tests" / "exclusive_settings.txt";
    std::filesystem::create_directories(path.parent_path());
    std::string error;
    REQUIRE_MESSAGE(saveSettings(path.string(), settings, error), error);
    std::vector<std::string> warnings;
    CHECK_FALSE(loadSettings(path.string(), warnings).exclusiveInput);
    CHECK(warnings.empty());
}

TEST_CASE("hearing the instrument: on by default, its own sound through a tone, kept"){
    Settings settings;
    CHECK(settings.monitorOn);
    CHECK_FALSE(settings.monitorSynth); // its own sound: no delay, unlike the synth
    CHECK(settings.bassTone == "Clean");
    CHECK(settings.guitarTone == "Clean");
    settings.monitorOn = false;
    settings.monitorSynth = true;
    settings.bassTone = "Sunday growl 2";
    settings.guitarTone = "Space";
    settings.heardInstrument = InputRole::Guitar;
    std::string path = settingsPath("monitor.txt"), error;
    REQUIRE(saveSettings(path, settings, error));
    std::vector<std::string> warnings;
    Settings loaded = loadSettings(path, warnings);
    CHECK(warnings.empty());
    CHECK_FALSE(loaded.monitorOn);
    CHECK(loaded.monitorSynth);
    CHECK(loaded.bassTone == "Sunday growl 2"); // spaces and all
    CHECK(loaded.guitarTone == "Space");        // each instrument its own
    CHECK(loaded.heardInstrument == InputRole::Guitar);
    CHECK(loaded.toneFor(InputRole::Bass) == "Sunday growl 2");

    // One tone for both, from before: both instruments keep it
    std::ofstream(path, std::ios::binary) << "version 1\nmonitor_tone_name Dub\n";
    Settings older = loadSettings(path, warnings);
    CHECK(older.bassTone == "Dub");
    CHECK(older.guitarTone == "Dub");

    // The small amp's settings, from before tones, are no trouble
    std::ofstream(path, std::ios::binary) << "version 1\nmonitor_drive 0.4\nmonitor_tone 0.25\n";
    loadSettings(path, warnings);
    CHECK(warnings.empty());
}
