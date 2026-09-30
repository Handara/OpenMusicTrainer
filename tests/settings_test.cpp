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
    original.previewSound = "my rain.wav";
    original.fullscreen = true;
    original.darkTheme = true;
    original.lowStringOnTop = false;
    original.noteViews.staff = true; // with the highway
    original.noteViews.tab = true;
    original.noteViews.highwayFalls = true;
    original.frameRateLimit = 144;
    original.noteSpeed = 450.0f;
    original.globalOffsetMs = -35;
    original.inputOffsetMs = 22;
    original.playWithInstrument = true;

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
    CHECK(loaded.previewSound == "my rain.wav");
    CHECK(loaded.fullscreen);
    CHECK(loaded.darkTheme);
    CHECK_FALSE(loaded.lowStringOnTop);
    CHECK(loaded.noteViews.staff);
    CHECK(loaded.noteViews.tab);
    CHECK(loaded.noteViews.highway);
    CHECK(loaded.noteViews.highwayFalls);
    CHECK(loaded.frameRateLimit == 144);
    CHECK(loaded.noteSpeed == doctest::Approx(450.0f));
    CHECK(loaded.globalOffsetMs == -35);
    CHECK(loaded.inputOffsetMs == 22);
    CHECK(loaded.playWithInstrument);
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

TEST_CASE("note views: any mix, and the older one-word form"){
    struct Case { const char* line; bool staff, tab, highway; size_t warnings; };
    const Case cases[] = {
        {"note_view staff",          true,  false, false, 0},
        {"note_view staff tab",      true,  true,  false, 0},
        {"note_view highway staff",  true,  false, true,  0},
        {"note_view both",           true,  false, true,  0}, // how files first wrote it
        {"note_view",                false, false, true,  1}, // nothing on: keep the default
        {"note_view tab piano",      false, false, true,  1}, // an unknown word: keep the default, not half of it
    };
    for (const Case& c : cases){
        CAPTURE(c.line);
        std::string path = settingsPath("views.txt");
        std::ofstream(path, std::ios::binary) << "version 1\n" << c.line << "\n";
        std::vector<std::string> warnings;
        Settings settings = loadSettings(path, warnings);
        CHECK(settings.noteViews.staff == c.staff);
        CHECK(settings.noteViews.tab == c.tab);
        CHECK(settings.noteViews.highway == c.highway);
        CHECK(warnings.size() == c.warnings);
    }
}

TEST_CASE("the neck view is kept, alone or with the others"){
    for (const char* line : { "note_view neck", "note_view highway neck" }){
        CAPTURE(line);
        std::string path = settingsPath("neck.txt");
        std::ofstream(path, std::ios::binary) << "version 1\n" << line << "\n";
        std::vector<std::string> warnings;
        Settings settings = loadSettings(path, warnings);
        CHECK(warnings.empty());
        CHECK(settings.noteViews.neck);
        std::string error;
        REQUIRE(saveSettings(path, settings, error));
        CHECK(loadSettings(path, warnings).noteViews.neck); // written back and read again
    }
    CHECK_FALSE(Settings{}.noteViews.neck);
}

TEST_CASE("the highway's direction is kept whatever order the lines come in"){
    std::string path = settingsPath("direction.txt");
    std::ofstream(path, std::ios::binary) << "version 1\nhighway_direction falling\nnote_view tab highway\n";
    std::vector<std::string> warnings;
    Settings settings = loadSettings(path, warnings);
    CHECK(warnings.empty());
    CHECK(settings.noteViews.highwayFalls); // reading note_view after it doesn't reset it
    CHECK(settings.noteViews.tab);
    CHECK_FALSE(Settings{}.noteViews.highwayFalls);  // across by default
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

TEST_CASE("hearing the instrument: on by default, its amp kept"){
    Settings settings;
    CHECK(settings.monitorOn);
    settings.monitorOn = false;
    settings.monitorDrive = 0.4f;
    settings.monitorTone = 0.25f;
    std::string path = settingsPath("monitor.txt"), error;
    REQUIRE(saveSettings(path, settings, error));
    std::vector<std::string> warnings;
    Settings loaded = loadSettings(path, warnings);
    CHECK(warnings.empty());
    CHECK_FALSE(loaded.monitorOn);
    CHECK(loaded.monitorDrive == doctest::Approx(0.4f));
    CHECK(loaded.monitorTone == doctest::Approx(0.25f));
}
