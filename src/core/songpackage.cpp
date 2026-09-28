#include "core/songpackage.h"

#include "core/chart.h"
#include "core/files.h"
#include "miniz.h"

#include <filesystem>

namespace fs = std::filesystem;

const char* const CHART_FILE = "song.chart";
const char* const COVER_FILES[] = { "cover.png", "cover.jpg" };

bool exportSongPackage(const std::string& songFolder, const std::string& packagePath, std::string& error){
    fs::path folder = songFolder;
    Chart chart;
    if (!loadChart((folder / CHART_FILE).string(), chart, error)) return false;

    // The chart, its audio, and a cover if there's one
    std::vector<std::string> files = { CHART_FILE };
    if (!chart.audioFile.empty()) files.push_back(chart.audioFile);
    for (const char* cover : COVER_FILES) if (fs::is_regular_file(folder / cover)) files.push_back(cover);
    for (const std::string& file : files){
        if (!fs::is_regular_file(folder / file)){
            error = "The song's folder has no '" + file + "'";
            return false;
        }
    }

    // Written next to the destination first, then swapped in: a failure never leaves half a package
    std::string tempPath = packagePath + ".tmp";
    mz_zip_archive zip{};
    if (!mz_zip_writer_init_file(&zip, tempPath.c_str(), 0)){
        error = "Could not write '" + packagePath + "'";
        return false;
    }
    bool ok = true;
    for (const std::string& file : files){
        if (!mz_zip_writer_add_file(&zip, file.c_str(), (folder / file).string().c_str(), nullptr, 0, MZ_DEFAULT_LEVEL)){
            error = "Could not add '" + file + "' to the package";
            ok = false;
            break;
        }
    }
    ok = ok && mz_zip_writer_finalize_archive(&zip);
    mz_zip_writer_end(&zip);
    std::error_code ec;
    if (ok) fs::rename(tempPath, packagePath, ec);
    if (!ok || ec){
        if (ok) error = "Could not write '" + packagePath + "': " + ec.message();
        fs::remove(tempPath, ec);
        return false;
    }
    return true;
}

// A name that stays in the folder it's unpacked into: no folders, no "..", nothing hidden
static bool plainFileName(const std::string& name){
    if (name.empty() || name[0] == '.') return false;
    for (char c : name) if (c == '/' || c == '\\' || c == ':') return false;
    return true;
}

bool installSongPackage(const std::string& packagePath, const std::string& songsDir, std::string& installedFolder,
                        std::string& error){
    mz_zip_archive zip{};
    if (!mz_zip_reader_init_file(&zip, packagePath.c_str(), 0)){
        error = "'" + fs::path(packagePath).filename().string() + "' isn't a song package (it can't be opened as a zip)";
        return false;
    }
    std::error_code ec;
    fs::create_directories(songsDir, ec);
    // Unpacked into a hidden folder first; it becomes the song's folder only once everything checks out
    fs::path staging = fs::path(songsDir) / ".installing";
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
        std::string name = stat.m_filename;
        if (stat.m_is_directory || !plainFileName(name)) return fail("The package holds '" + name + "': a song package holds only plain files");
        if (!mz_zip_reader_extract_to_file(&zip, i, (staging / name).string().c_str(), 0)) return fail("Could not unpack '" + name + "'");
    }
    mz_zip_reader_end(&zip);

    Chart chart;
    std::string chartError;
    if (!fs::is_regular_file(staging / CHART_FILE)) return fail("The package has no song.chart");
    if (!loadChart((staging / CHART_FILE).string(), chart, chartError)){
        // Point at the file inside the package, not the staging folder it was checked in
        std::string where = (staging / CHART_FILE).string();
        if (chartError.rfind(where, 0) == 0) chartError = CHART_FILE + chartError.substr(where.size());
        return fail("In the package, " + chartError);
    }
    if (chart.audioFile.empty() || !fs::is_regular_file(staging / chart.audioFile)){
        return fail("The package has no '" + chart.audioFile + "', the audio its chart names");
    }

    // The song's own folder: its title, or the package's name, made safe; " (2)" and on if it's taken
    std::string name = safeFolderName(chart.title);
    if (name.empty()) name = safeFolderName(fs::path(packagePath).stem().string());
    if (name.empty()) name = "Song";
    fs::path destination = fs::path(songsDir) / name;
    for (int n = 2; fs::exists(destination); n++) destination = fs::path(songsDir) / (name + " (" + std::to_string(n) + ")");
    fs::rename(staging, destination, ec);
    if (ec) return fail("Could not install the song: " + ec.message());
    installedFolder = destination.string();
    return true;
}
