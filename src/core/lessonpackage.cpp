#include "core/lessonpackage.h"

#include "core/course.h"
#include "core/files.h"
#include "core/lessondoc.h"
#include "miniz.h"

#include <filesystem>
#include <vector>

namespace fs = std::filesystem;

// A name that stays in the folder it's unpacked into: no folders, no "..", nothing hidden
static bool plainFileName(const std::string& name){
    if (name.empty() || name[0] == '.') return false;
    for (char c : name) if (c == '/' || c == '\\' || c == ':') return false;
    return true;
}

bool exportLessonPackage(const std::string& lessonFolder, const std::string& packagePath, std::string& error){
    LessonDoc doc;
    if (!loadLessonDoc(lessonFolder, doc, error)) return false;
    // The folder's files, as they are (its lesson and what its blocks show; nothing hidden)
    std::vector<std::string> files;
    std::error_code ec;
    for (const fs::directory_entry& file : fs::directory_iterator(lessonFolder, ec))
        if (file.is_regular_file() && plainFileName(file.path().filename().string())) files.push_back(file.path().filename().string());
    // Written next to the destination first, then swapped in: a failure never leaves half a package
    const std::string tempPath = packagePath + ".tmp";
    mz_zip_archive zip{};
    if (!mz_zip_writer_init_file(&zip, tempPath.c_str(), 0)){
        error = "Couldn't write '" + packagePath + "'";
        return false;
    }
    bool ok = true;
    for (const std::string& file : files){
        if (!mz_zip_writer_add_file(&zip, file.c_str(), (fs::path(lessonFolder) / file).string().c_str(), nullptr, 0, MZ_DEFAULT_LEVEL)){
            error = "Couldn't add '" + file + "' to the package";
            ok = false;
            break;
        }
    }
    ok = ok && mz_zip_writer_finalize_archive(&zip);
    mz_zip_writer_end(&zip);
    if (ok) fs::rename(tempPath, packagePath, ec);
    if (!ok || ec){
        if (ok) error = "Couldn't write '" + packagePath + "': " + ec.message();
        fs::remove(tempPath, ec);
        return false;
    }
    return true;
}

bool installLessonPackage(const std::string& packagePath, const std::string& lessonsDir, std::string& installedFolder, std::string& error){
    mz_zip_archive zip{};
    if (!mz_zip_reader_init_file(&zip, packagePath.c_str(), 0)){
        error = "'" + fs::path(packagePath).filename().string() + "' isn't a lesson package (it can't be opened as a zip)";
        return false;
    }
    std::error_code ec;
    fs::create_directories(lessonsDir, ec);
    // Unpacked into a hidden folder first; it becomes the lesson's folder only once everything checks out
    const fs::path staging = fs::path(lessonsDir) / ".installing";
    fs::remove_all(staging, ec);
    fs::create_directories(staging, ec);
    auto fail = [&](const std::string& message){
        error = message;
        mz_zip_reader_end(&zip);
        std::error_code ignored;
        fs::remove_all(staging, ignored);
        return false;
    };
    for (mz_uint i = 0; i < mz_zip_reader_get_num_files(&zip); i++){
        mz_zip_archive_file_stat stat;
        if (!mz_zip_reader_file_stat(&zip, i, &stat)) return fail("The package is damaged");
        const std::string name = stat.m_filename;
        if (stat.m_is_directory || !plainFileName(name)) return fail("The package holds '" + name + "': a lesson package holds only plain files");
        if (!mz_zip_reader_extract_to_file(&zip, i, (staging / name).string().c_str(), 0)) return fail("Couldn't unpack '" + name + "'");
    }
    mz_zip_reader_end(&zip);
    LessonDoc doc;
    std::string lessonError;
    if (!fs::is_regular_file(staging / LESSON_FILE_NAME)) return fail("The package has no lesson.lesson");
    if (!loadLessonDoc(staging.string(), doc, lessonError)){
        // Point at the file inside the package, not the folder it was checked in
        const std::string where = (staging / LESSON_FILE_NAME).string();
        if (lessonError.rfind(where, 0) == 0) lessonError = LESSON_FILE_NAME + lessonError.substr(where.size());
        return fail("In the package, " + lessonError);
    }
    // The lesson's own folder: its title, or the package's name, made safe; " (2)" and on if it's taken
    std::string name = safeFolderName(doc.title);
    if (name.empty()) name = safeFolderName(fs::path(packagePath).stem().string());
    if (name.empty()) name = "Lesson";
    fs::path destination = fs::path(lessonsDir) / name;
    for (int n = 2; fs::exists(destination); n++) destination = fs::path(lessonsDir) / (name + " (" + std::to_string(n) + ")");
    fs::rename(staging, destination, ec);
    if (ec) return fail("Couldn't install the lesson: " + ec.message());
    installedFolder = destination.string();
    return true;
}

bool installCourseFile(const std::string& coursePath, const std::string& coursesDir, std::string& installedPath, std::string& error){
    Course course;
    if (!loadCourse(coursePath, course, error)) return false;
    std::error_code ec;
    fs::create_directories(coursesDir, ec);
    const std::string stem = fs::path(coursePath).stem().string();
    fs::path destination = fs::path(coursesDir) / (stem + COURSE_EXTENSION);
    for (int n = 2; fs::exists(destination); n++) destination = fs::path(coursesDir) / (stem + " (" + std::to_string(n) + ")" + COURSE_EXTENSION);
    fs::copy_file(coursePath, destination, ec);
    if (ec){
        error = "Couldn't install the course: " + ec.message();
        return false;
    }
    installedPath = destination.string();
    return true;
}
