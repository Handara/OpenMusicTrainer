#include "doctest/doctest.h"

#include "core/pianokeys.h"
#include "core/settings.h"

#include <filesystem>
#include <set>

TEST_CASE("the default layout: 29 notes, every key once"){
    std::vector<std::string> keys = defaultPianoKeys();
    REQUIRE((int)keys.size() == PIANO_KEY_SLOTS);
    CHECK(std::set<std::string>(keys.begin(), keys.end()).size() == keys.size());
    CHECK(keys[0] == "Z");   // C
    CHECK(keys[1] == "S");   // C#
    CHECK(keys[12] == "Q");  // the next C
    CHECK(keys[28] == "P");  // E, an octave and a third up
}

TEST_CASE("where the layout starts, and rebinding"){
    CHECK(pianoBaseFor(60) == 60);
    CHECK(pianoBaseFor(64) == 60);
    CHECK(pianoBaseFor(59) == 48);

    std::vector<std::string> keys = defaultPianoKeys();
    bindPianoKey(keys, 0, "A");      // C on A instead of Z
    CHECK(keys[0] == "A");
    bindPianoKey(keys, 2, "A");      // then D takes A: C loses it
    CHECK(keys[2] == "A");
    CHECK(keys[0] == "none");
}

TEST_CASE("the piano keys are kept in the settings"){
    Settings settings;
    CHECK(settings.pianoKeys == defaultPianoKeys());
    bindPianoKey(settings.pianoKeys, 4, ",");
    std::filesystem::path path = std::filesystem::temp_directory_path() / "lahn_tests" / "piano_settings.txt";
    std::filesystem::create_directories(path.parent_path());
    std::string error;
    REQUIRE_MESSAGE(saveSettings(path.string(), settings, error), error);
    std::vector<std::string> warnings;
    Settings loaded = loadSettings(path.string(), warnings);
    CHECK(warnings.empty());
    CHECK(loaded.pianoKeys == settings.pianoKeys);
    CHECK(loaded.pianoKeys[4] == ",");
}
