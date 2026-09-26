#include "core/files.h"

#include <filesystem>
#include <fstream>

bool writeFileAtomically(const std::string& path, const std::string& content, std::string& error){
    std::string tempPath = path + ".tmp";
    {
        std::ofstream file(tempPath, std::ios::binary); // binary: same "\n" line endings on every OS
        file << content;
        if (!file){
            error = tempPath + ": could not write file";
            return false;
        }
    } // closing the file here flushes it before the rename
    std::error_code ec;
    std::filesystem::rename(tempPath, path, ec);
    if (ec){
        error = path + ": could not replace file: " + ec.message();
        return false;
    }
    return true;
}
