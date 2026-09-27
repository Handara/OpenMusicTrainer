#include "core/paths.h"

#include <cstdlib>
#include <filesystem>

namespace fs = std::filesystem;

const char* const APP_FOLDER_NAME = "lahn";
const char* const OLD_APP_FOLDER_NAME = "OpenMusicTrainer"; // the game's first name

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

std::string oldUserDataDir(){
    return (dataBase() / OLD_APP_FOLDER_NAME).string();
}

bool moveUserDataFolder(const std::string& from, const std::string& to, std::string& error){
    std::error_code ec;
    if (!fs::is_directory(from, ec) || fs::exists(to, ec)) return true; // nothing to move, or already moved
    fs::rename(from, to, ec); // one rename: the folder moves whole, or not at all
    if (ec){
        error = "Could not move " + from + " to " + to + ": " + ec.message();
        return false;
    }
    return true;
}
