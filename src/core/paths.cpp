#include "core/paths.h"

#include <cstdlib>
#include <filesystem>

namespace fs = std::filesystem;

const char* const APP_FOLDER_NAME = "OpenMusicTrainer";

// Reads an environment variable, empty if it isn't set
static std::string environment(const char* name){
    const char* value = std::getenv(name);
    return value ? value : "";
}

std::string userDataDir(){
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
    return (base / APP_FOLDER_NAME).string();
}
