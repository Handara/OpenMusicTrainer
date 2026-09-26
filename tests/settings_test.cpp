#include "doctest/doctest.h"

#include "core/settings.h"

#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

static std::string settingsPath(const std::string& name){
    fs::path dir = fs::temp_directory_path() / "omt_tests";
    fs::create_directories(dir);
    return (dir / name).string();
}

TEST_CASE("a missing settings file gives defaults without warnings"){
    std::vector<std::string> warnings;
    Settings settings = loadSettings(settingsPath("does_not_exist.txt"), warnings);
    CHECK(warnings.empty());
    CHECK(settings.previewSound == "pluck");
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
    original.lowStringOnTop = false;
    original.noteViews.staff = true; // with the highway
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
    CHECK_FALSE(loaded.lowStringOnTop);
    CHECK(loaded.noteViews.staff);
    CHECK(loaded.noteViews.highway);
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
    struct Case { const char* line; bool staff, highway; size_t warnings; };
    const Case cases[] = {
        {"note_view staff",          true,  false, 0},
        {"note_view highway staff",  true,  true,  0},
        {"note_view both",           true,  true,  0}, // how files first wrote it
        {"note_view",                false, true,  1}, // nothing on: keep the default
        {"note_view staff piano",    false, true,  1}, // an unknown word: keep the default, not half of it
    };
    for (const Case& c : cases){
        CAPTURE(c.line);
        std::string path = settingsPath("views.txt");
        std::ofstream(path, std::ios::binary) << "version 1\n" << c.line << "\n";
        std::vector<std::string> warnings;
        Settings settings = loadSettings(path, warnings);
        CHECK(settings.noteViews.staff == c.staff);
        CHECK(settings.noteViews.highway == c.highway);
        CHECK(warnings.size() == c.warnings);
    }
}
