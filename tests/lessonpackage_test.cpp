#include "doctest/doctest.h"

#include "core/lessondoc.h"
#include "core/lessonpackage.h"

#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

static fs::path packageDir(const std::string& name){
    const fs::path dir = fs::temp_directory_path() / "lahn_tests" / "lessonpackages" / name;
    fs::remove_all(dir);
    fs::create_directories(dir);
    return dir;
}

TEST_CASE("lesson packages: a lesson and its files in one, installed in a folder of its own"){
    const fs::path root = packageDir("roundtrip");
    const fs::path lesson = root / "made";
    fs::create_directories(lesson);
    std::ofstream(lesson / LESSON_FILE_NAME) << "version 2\ntitle Meet F\npage P\nblock image\nfile f.png\ncaption F\n";
    std::ofstream(lesson / "f.png") << "a picture";
    std::string error;
    const std::string package = (root / ("Meet F" + std::string(LESSON_PACKAGE_EXTENSION))).string();
    REQUIRE_MESSAGE(exportLessonPackage(lesson.string(), package, error), error);
    CHECK(fs::exists(package));
    CHECK_FALSE(fs::exists(package + ".tmp"));

    std::string installed;
    REQUIRE_MESSAGE(installLessonPackage(package, (root / "lessons").string(), installed, error), error);
    CHECK(fs::path(installed).filename() == "Meet F");
    CHECK(fs::exists(fs::path(installed) / "f.png"));
    LessonDoc doc;
    REQUIRE_MESSAGE(loadLessonDoc(installed, doc, error), error);
    CHECK(doc.title == "Meet F");
    // Again: a folder of its own
    REQUIRE(installLessonPackage(package, (root / "lessons").string(), installed, error));
    CHECK(fs::path(installed).filename() == "Meet F (2)");
    CHECK_FALSE(fs::exists(root / "lessons" / ".installing"));

    // A lesson that doesn't play isn't packed; a file that isn't a package isn't installed
    std::ofstream(lesson / LESSON_FILE_NAME) << "version 2\ntitle Broken\npage P\nblock image\nfile missing.png\n";
    CHECK_FALSE(exportLessonPackage(lesson.string(), (root / "broken.lahnlesson").string(), error));
    std::ofstream(root / "not.lahnlesson") << "not a zip";
    CHECK_FALSE(installLessonPackage((root / "not.lahnlesson").string(), (root / "lessons").string(), installed, error));
    CHECK(error.find("isn't a lesson package") != std::string::npos);
}

TEST_CASE("course files: installed once they read, under a name of their own"){
    const fs::path root = packageDir("courses");
    std::ofstream(root / "mine.course") << "version 2\ntitle Mine\nlevel L\nchapter C\npage\nblock text\ntext Hello.\n";
    std::string installed, error;
    REQUIRE_MESSAGE(installCourseFile((root / "mine.course").string(), (root / "installed").string(), installed, error), error);
    CHECK(fs::path(installed).filename() == "mine.course");
    REQUIRE(installCourseFile((root / "mine.course").string(), (root / "installed").string(), installed, error));
    CHECK(fs::path(installed).filename() == "mine (2).course");
    std::ofstream(root / "bad.course") << "version 2\ntitle Bad\n";
    CHECK_FALSE(installCourseFile((root / "bad.course").string(), (root / "installed").string(), installed, error));
}
