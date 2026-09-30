#include "core/tonelibrary.h"

#include "core/files.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

static bool readFile(const std::string& path, std::string& text){
    std::ifstream file(path, std::ios::binary);
    if (!file) return false;
    std::ostringstream content;
    content << file.rdbuf();
    text = content.str();
    return true;
}

std::vector<Tone> loadUserTones(const std::string& folder, std::vector<std::string>& problems){
    std::vector<Tone> tones;
    std::error_code ec;
    if (!fs::is_directory(folder, ec)) return tones;
    std::vector<fs::path> files;
    for (const fs::directory_entry& entry : fs::directory_iterator(folder, ec)){
        if (entry.is_regular_file(ec) && entry.path().extension() == TONE_FILE_EXTENSION) files.push_back(entry.path());
    }
    std::sort(files.begin(), files.end());
    for (const fs::path& file : files){
        std::string text, error;
        Tone tone;
        if (!readFile(file.string(), text) || !readTone(text, tone, error)){
            problems.push_back(file.filename().string() + ": " + (error.empty() ? "can't be read" : error));
            continue;
        }
        if (tone.name.empty()) tone.name = file.stem().string(); // a file without its name line goes by the file's
        tones.push_back(tone);
    }
    return tones;
}

std::string tonePath(const std::string& folder, const std::string& name){
    std::string safe = safeFolderName(name);
    if (safe.empty()) safe = "tone";
    return (fs::path(folder) / (safe + TONE_FILE_EXTENSION)).string();
}

bool saveUserTone(const std::string& folder, const Tone& tone, std::string& error){
    std::error_code ec;
    fs::create_directories(folder, ec);
    return writeFileAtomically(tonePath(folder, tone.name), writeTone(tone), error);
}

bool deleteUserTone(const std::string& folder, const std::string& name, std::string& error){
    std::error_code ec;
    if (!fs::remove(tonePath(folder, name), ec) || ec){
        error = "Couldn't delete the tone " + name + (ec ? ": " + ec.message() : "");
        return false;
    }
    return true;
}

static bool nameTaken(const std::string& name, const std::vector<Tone>& userTones){
    if (findBuiltInTone(name)) return true;
    for (const Tone& tone : userTones){
        if (tone.name == name || safeFolderName(tone.name) == safeFolderName(name)) return true; // one file each
    }
    return false;
}

std::string freeToneName(const std::string& wanted, const std::vector<Tone>& userTones){
    std::string base = wanted.empty() ? "My tone" : wanted;
    if (!nameTaken(base, userTones)) return base;
    for (int n = 2;; n++){
        std::string name = base + " " + std::to_string(n);
        if (!nameTaken(name, userTones)) return name;
    }
}

bool importTone(const std::string& path, const std::string& folder, const std::vector<Tone>& userTones, Tone& imported,
                std::string& error){
    std::string text;
    if (!readFile(path, text)){
        error = "Couldn't read " + fs::path(path).filename().string();
        return false;
    }
    Tone tone;
    if (!readTone(text, tone, error)){
        error = fs::path(path).filename().string() + " is " + error;
        return false;
    }
    if (tone.name.empty()) tone.name = fs::path(path).stem().string();
    tone.name = freeToneName(tone.name, userTones);
    if (!saveUserTone(folder, tone, error)) return false;
    imported = tone;
    return true;
}

Tone findTone(const std::string& name, const std::vector<Tone>& userTones){
    for (const Tone& tone : userTones) if (tone.name == name) return tone;
    if (const Tone* builtIn = findBuiltInTone(name)) return *builtIn;
    return builtInTones().front();
}
