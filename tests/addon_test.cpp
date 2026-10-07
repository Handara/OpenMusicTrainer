#include "doctest/doctest.h"

#include "core/addon.h"
#include "miniz.h"

#include <filesystem>
#include <fstream>
#include <map>

namespace fs = std::filesystem;

static std::string makeZip(const std::string& name, const std::map<std::string, std::string>& files){
    fs::path path = fs::temp_directory_path() / "lahn_tests" / name;
    fs::create_directories(path.parent_path());
    fs::remove(path);
    mz_zip_archive zip{};
    REQUIRE(mz_zip_writer_init_file(&zip, path.string().c_str(), 0));
    for (const auto& [file, content] : files) REQUIRE(mz_zip_writer_add_mem(&zip, file.c_str(), content.data(), content.size(), MZ_DEFAULT_LEVEL));
    REQUIRE(mz_zip_writer_finalize_archive(&zip));
    mz_zip_writer_end(&zip);
    return path.string();
}

TEST_CASE("an add-on installs into a folder of its name, and a newer one takes its place"){
    fs::path addons = fs::temp_directory_path() / "lahn_tests" / "addons";
    fs::remove_all(addons);
    AddonInfo info;
    CHECK_FALSE(readAddon(addons.string(), "stems", info)); // nothing there yet

    std::string error;
    std::string first = makeZip("stems1.lahnaddon", { { "addon.txt", "lahn_addon 1\nname stems\nversion 1\n" }, { "model.bin", "old" } });
    REQUIRE_MESSAGE(installAddon(first, addons.string(), info, error), error);
    CHECK(info.name == "stems");
    CHECK(info.version == 1);
    CHECK(fs::is_regular_file(addons / "stems" / "model.bin"));
    REQUIRE(readAddon(addons.string(), "stems", info));

    std::string second = makeZip("stems2.lahnaddon", { { "addon.txt", "lahn_addon 1\nname stems\nversion 2\n" }, { "new.bin", "new" } });
    REQUIRE_MESSAGE(installAddon(second, addons.string(), info, error), error);
    REQUIRE(readAddon(addons.string(), "stems", info));
    CHECK(info.version == 2);
    CHECK_FALSE(fs::exists(addons / "stems" / "model.bin")); // the old one's files are gone with it
    CHECK(fs::is_regular_file(addons / "stems" / "new.bin"));
}

TEST_CASE("what isn't an add-on isn't installed, and leaves nothing behind"){
    fs::path addons = fs::temp_directory_path() / "lahn_tests" / "addons_bad";
    fs::remove_all(addons);
    AddonInfo info;
    std::string error;
    // No addon.txt; a name that would leave the folder; a file in a folder; not a zip at all
    CHECK_FALSE(installAddon(makeZip("plain.zip", { { "song.chart", "x" } }), addons.string(), info, error));
    CHECK_FALSE(installAddon(makeZip("escape.zip", { { "addon.txt", "lahn_addon 1\nname ../up\nversion 1\n" } }), addons.string(), info, error));
    CHECK_FALSE(installAddon(makeZip("nested.zip", { { "addon.txt", "lahn_addon 1\nname stems\n" }, { "sub/file", "x" } }), addons.string(), info, error));
    fs::path notZip = fs::temp_directory_path() / "lahn_tests" / "not.lahnaddon";
    { std::ofstream(notZip) << "hello"; }
    CHECK_FALSE(installAddon(notZip.string(), addons.string(), info, error));
    CHECK(!error.empty());
    CHECK_FALSE(fs::exists(addons / "stems"));
    CHECK_FALSE(fs::exists(addons / ".installing"));
}
