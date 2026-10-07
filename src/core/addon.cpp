#include "core/addon.h"

#include "miniz.h"

#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

const char* const MANIFEST = "addon.txt";

static bool readManifest(const fs::path& file, AddonInfo& info){
    std::ifstream in(file);
    std::string line;
    bool isAddon = false;
    while (std::getline(in, line)){
        std::istringstream words(line);
        std::string key;
        if (!(words >> key)) continue;
        if (key == "hardthz_addon" || key == "lahn_addon") isAddon = true; // (lahn: the game's name before)
        else if (key == "name") words >> info.name;
        else if (key == "version") words >> info.version;
    }
    // Its name becomes a folder's: letters, digits, - and _ only
    for (char c : info.name) if (!(std::isalnum((unsigned char)c) || c == '-' || c == '_')) return false;
    return isAddon && !info.name.empty();
}

bool readAddon(const std::string& addonsDir, const std::string& name, AddonInfo& info){
    info = AddonInfo{};
    return readManifest(fs::path(addonsDir) / name / MANIFEST, info) && info.name == name;
}

// A name that stays in the folder it's unpacked into: no folders, no "..", nothing hidden
static bool plainFileName(const std::string& name){
    if (name.empty() || name[0] == '.') return false;
    for (char c : name) if (c == '/' || c == '\\' || c == ':') return false;
    return true;
}

bool installAddon(const std::string& zipPath, const std::string& addonsDir, AddonInfo& installed, std::string& error){
    mz_zip_archive zip{};
    if (!mz_zip_reader_init_file(&zip, zipPath.c_str(), 0)){
        error = "'" + fs::path(zipPath).filename().string() + "' isn't an add-on (it can't be opened)";
        return false;
    }
    std::error_code ec;
    fs::create_directories(addonsDir, ec);
    // Unpacked into a hidden folder first; it takes its place only once everything checks out
    fs::path staging = fs::path(addonsDir) / ".installing";
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
        if (!mz_zip_reader_file_stat(&zip, i, &stat)) return fail("The add-on is damaged");
        std::string name = stat.m_filename;
        if (stat.m_is_directory || !plainFileName(name)) return fail("The add-on holds '" + name + "': an add-on holds only plain files");
        if (!mz_zip_reader_extract_to_file(&zip, i, (staging / name).string().c_str(), 0)) return fail("Could not unpack '" + name + "' (is the disk full?)");
    }
    mz_zip_reader_end(&zip);
    AddonInfo info;
    if (!readManifest(staging / MANIFEST, info)) return fail("It has no addon.txt saying what it is: not a hardthz add-on");
    fs::path destination = fs::path(addonsDir) / info.name;
    fs::remove_all(destination, ec); // a newer one takes the older one's place
    fs::rename(staging, destination, ec);
    if (ec) return fail("Could not install the add-on: " + ec.message());
    installed = info;
    return true;
}
