#include "doctest/doctest.h"

#include "core/chart.h"
#include "core/songpackage.h"
#include "miniz.h"

#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

static const char* CHART = "version 2\ntitle Night Drive\nartist Me\naudio track.ogg\nresolution 480\nend 1920\n"
                           "tempo 0 120\ntrack guitar Lead\ntuning 40 45 50 55 59 64\nn 0 0 3\n";

static fs::path freshDir(const std::string& name){
    fs::path dir = fs::temp_directory_path() / "lahn_tests" / "packages" / name;
    fs::remove_all(dir);
    fs::create_directories(dir);
    return dir;
}

static std::string readFile(const fs::path& path){
    std::ifstream file(path, std::ios::binary);
    std::stringstream content;
    content << file.rdbuf();
    return content.str();
}

// A song folder: its chart, stand-in audio and a cover, and a file that isn't part of the song
static fs::path songFolder(){
    fs::path song = freshDir("source") / "Night Drive";
    fs::create_directories(song);
    std::ofstream(song / "song.chart", std::ios::binary) << CHART;
    std::ofstream(song / "track.ogg", std::ios::binary) << std::string(5000, 'a') << "the end";
    std::ofstream(song / "cover.png", std::ios::binary) << "picture";
    std::ofstream(song / "notes.txt", std::ios::binary) << "my own notes";
    return song;
}

TEST_CASE("a song goes into a package and comes out the same"){
    fs::path song = songFolder();
    fs::path package = freshDir("out") / "Night Drive.lahn";
    std::string error;
    REQUIRE_MESSAGE(exportSongPackage(song.string(), package.string(), error), error);
    CHECK(fs::exists(package));
    CHECK_FALSE(fs::exists(package.string() + ".tmp"));

    fs::path songs = freshDir("songs");
    std::string installed;
    REQUIRE_MESSAGE(installSongPackage(package.string(), songs.string(), installed, error), error);
    CHECK(installed == (songs / "Night Drive").string());
    CHECK(readFile(songs / "Night Drive" / "song.chart") == CHART);
    CHECK(readFile(songs / "Night Drive" / "track.ogg") == readFile(song / "track.ogg"));
    CHECK(readFile(songs / "Night Drive" / "cover.png") == "picture");
    CHECK_FALSE(fs::exists(songs / "Night Drive" / "notes.txt")); // only the song's own files travel
    CHECK_FALSE(fs::exists(songs / ".installing"));

    // Installed again: a second folder, the first untouched
    REQUIRE_MESSAGE(installSongPackage(package.string(), songs.string(), installed, error), error);
    CHECK(installed == (songs / "Night Drive (2)").string());
    CHECK(fs::exists(songs / "Night Drive" / "song.chart"));
}

// A zip made by hand, with whatever names and contents a test needs
static fs::path handMadePackage(const std::vector<std::pair<std::string, std::string>>& files){
    fs::path path = freshDir("handmade") / "odd.lahn";
    mz_zip_archive zip{};
    REQUIRE(mz_zip_writer_init_file(&zip, path.string().c_str(), 0));
    for (const auto& [name, content] : files){
        REQUIRE(mz_zip_writer_add_mem(&zip, name.c_str(), content.data(), content.size(), MZ_DEFAULT_LEVEL));
    }
    REQUIRE(mz_zip_writer_finalize_archive(&zip));
    mz_zip_writer_end(&zip);
    return path;
}

TEST_CASE("a package is refused, leaving nothing behind, when"){
    fs::path songs = freshDir("refused");
    std::string installed, error;
    fs::path package;
    SUBCASE("a name would land outside the song's folder"){
        package = handMadePackage({{"song.chart", CHART}, {"track.ogg", "x"}, {"../evil.txt", "x"}});
    }
    SUBCASE("it has folders"){
        package = handMadePackage({{"song.chart", CHART}, {"track.ogg", "x"}, {"extra/more.txt", "x"}});
    }
    SUBCASE("there's no chart"){
        package = handMadePackage({{"track.ogg", "x"}});
    }
    SUBCASE("the chart names audio that isn't there"){
        package = handMadePackage({{"song.chart", CHART}});
    }
    SUBCASE("the chart is broken"){
        package = handMadePackage({{"song.chart", "version 2\ntitle Broken\n"}, {"track.ogg", "x"}});
    }
    SUBCASE("it isn't a zip at all"){
        package = freshDir("notzip") / "fake.lahn";
        std::ofstream(package, std::ios::binary) << "just text";
    }
    CHECK_FALSE(installSongPackage(package.string(), songs.string(), installed, error));
    CHECK_FALSE(error.empty());
    CHECK(fs::is_empty(songs));
    CHECK_FALSE(fs::exists(songs.parent_path() / "evil.txt"));
}
