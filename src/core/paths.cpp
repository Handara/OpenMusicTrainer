#include "core/paths.h"

#include <cstdlib>
#include <filesystem>

namespace fs = std::filesystem;

const char* const APP_FOLDER_NAME = "hardthz";
const char* const OLD_APP_FOLDER_NAMES[] = { "lahn", "OpenMusicTrainer" }; // the game's earlier names, the latest first

// Reads an environment variable, empty if it isn't set
static std::string environment(const char* name){
    const char* value = std::getenv(name);
    return value ? value : "";
}

// The OS's per-user data folder, which holds one folder per app
static fs::path dataBase(){
    fs::path base;
#if defined(_WIN32)
    base = environment("APPDATA");
#elif defined(__APPLE__)
    if (!environment("HOME").empty()) base = fs::path(environment("HOME")) / "Library" / "Application Support";
#else
    base = environment("XDG_DATA_HOME");
    if (base.empty() && !environment("HOME").empty()) base = fs::path(environment("HOME")) / ".local" / "share";
#endif
    if (base.empty()) base = fs::current_path(); // no home folder at all (rare): keep data next to where we run
    return base;
}

std::string userDataDir(){
    return (dataBase() / APP_FOLDER_NAME).string();
}

std::vector<std::string> oldUserDataDirs(){
    std::vector<std::string> dirs;
    for (const char* name : OLD_APP_FOLDER_NAMES) dirs.push_back((dataBase() / name).string());
    return dirs;
}

// No file anywhere in it: only the empty folders a start makes (one where the move failed left the new name like
// that, and the player's files still under the old one)
static bool holdsNoFiles(const fs::path& dir){
    std::error_code ec;
    for (fs::recursive_directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec))
        if (it->is_regular_file(ec)) return false;
    return true;
}

bool moveUserDataFolder(const std::string& from, const std::string& to, std::string& error){
    std::error_code ec;
    if (!fs::is_directory(from, ec)) return true; // nothing to move
    if (fs::exists(to, ec)){
        if (!holdsNoFiles(to)) return true;       // already moved, or the player's own: never overwritten
        fs::remove_all(to, ec);
        if (ec){
            error = "Could not clear the empty " + to + ": " + ec.message();
            return false;
        }
    }
    fs::rename(from, to, ec); // one rename: the folder moves whole, or not at all
    if (!ec) return true;
    // Something holds a file in it (a window open on it, a virus scan): copied instead, the old one left as it was
    const std::string renameError = ec.message();
    ec.clear();
    fs::copy(from, to, fs::copy_options::recursive, ec);
    if (ec){
        error = "Could not move " + from + " to " + to + " (" + renameError + "), nor copy it: " + ec.message();
        std::error_code ignored;
        fs::remove_all(to, ignored); // half a copy: tried again next time, from the start
        return false;
    }
    return true;
}
