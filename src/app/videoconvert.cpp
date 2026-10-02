#include "app/videoconvert.h"

#include "app/process.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <vector>

namespace fs = std::filesystem;

#ifdef _WIN32
const char* const FFMPEG_FILE = "ffmpeg.exe";
#else
const char* const FFMPEG_FILE = "ffmpeg";
#endif
const char* const PICTURE_SIZE = "scale=-2:'min(480,ih)',fps=30"; // 480 lines at most, an even width, a frame rate MPEG-1 has
const char* const PICTURE_QUALITY = "6";  // 1 (best, biggest) to 31: behind the notes, washed out, this is plenty
const char* const SOUND_QUALITY = "2";    // an mp3 around 190 kbit/s

static std::string lowerExtension(const std::string& path){
    std::string extension = fs::path(path).extension().string();
    for (char& c : extension) c = (char)std::tolower((unsigned char)c);
    return extension;
}

bool isPlayableVideo(const std::string& path){
    std::string extension = lowerExtension(path);
    return extension == ".mpg" || extension == ".mpeg";
}

bool isVideoFile(const std::string& path){
    std::string extension = lowerExtension(path);
    for (const char* kind : { ".mp4", ".m4v", ".mkv", ".webm", ".mov", ".avi", ".wmv", ".flv", ".mpg", ".mpeg" }) if (extension == kind) return true;
    return false;
}

std::string findFfmpeg(const std::string& addonsDir){
    std::error_code ec;
    fs::path own = fs::path(addonsDir) / VIDEO_ADDON / FFMPEG_FILE;
    if (fs::is_regular_file(own, ec)){
        // Unpacked from the add-on it's a plain file: where programs are marked as such, it's marked
        fs::permissions(own, fs::perms::owner_exec | fs::perms::group_exec | fs::perms::others_exec, fs::perm_options::add, ec);
        return own.string();
    }
    return findProgram("ffmpeg");
}

// "Duration: 00:03:25.43, start: ..." as seconds; 0 if the line isn't that
static double durationIn(const std::string& line){
    size_t at = line.find("Duration: ");
    if (at == std::string::npos) return 0.0;
    int hours = 0, minutes = 0;
    double seconds = 0.0;
    if (std::sscanf(line.c_str() + at + 10, "%d:%d:%lf", &hours, &minutes, &seconds) != 3) return 0.0;
    return hours * 3600.0 + minutes * 60.0 + seconds;
}

// FFmpeg run on a file, writing `to`: into a file beside it first, which takes its name only once it's whole.
// FFmpeg says how long the file is as it opens it, then how far along it is (asked for with -progress).
static bool runFfmpeg(const std::string& ffmpeg, const std::string& from, const std::vector<std::string>& how, const std::string& to,
                      std::atomic<float>* progress, const std::atomic<bool>& cancel, std::string& error){
    const std::string unfinished = to + ".part";
    std::vector<std::string> arguments = { ffmpeg, "-y", "-hide_banner", "-nostdin", "-nostats", "-progress", "pipe:1", "-i", from };
    arguments.insert(arguments.end(), how.begin(), how.end());
    arguments.push_back(unfinished);

    double length = 0.0;
    std::string lastWords; // what it printed last that wasn't progress: when it fails, that's why
    bool nothingToWrite = false; // unless it said the file has no stream of the kind asked for (a video with no sound)
    bool ok = runProgram(arguments, [&](const std::string& line){
        if (length <= 0.0) length = durationIn(line);
        if (line.rfind("out_time_us=", 0) == 0){
            if (progress && length > 0.0) progress->store(std::clamp((float)(std::atof(line.c_str() + 12) / 1e6 / length), 0.0f, 1.0f));
        } else if (line.find('=') == std::string::npos || line.find(' ') != std::string::npos){
            lastWords = line; // progress lines are key=value, with no space
            if (line.find("does not contain any stream") != std::string::npos || line.find("matches no streams") != std::string::npos) nothingToWrite = true;
        }
    }, cancel, error);
    std::error_code ec;
    if (ok) fs::rename(unfinished, to, ec);
    if (!ok || ec){
        if (ok) error = "could not write " + to;
        else if (nothingToWrite) error = "nothing of that kind in it";
        else if (error.empty()) error = lastWords.empty() ? "FFmpeg could not read it" : lastWords;
        fs::remove(unfinished, ec);
        return false;
    }
    if (progress) progress->store(1.0f);
    return true;
}

bool convertVideo(const std::string& ffmpeg, const std::string& from, const std::string& to, std::atomic<float>* progress,
                  const std::atomic<bool>& cancel, std::string& error){
    return runFfmpeg(ffmpeg, from, { "-an", "-vf", PICTURE_SIZE, "-pix_fmt", "yuv420p", "-c:v", "mpeg1video", "-q:v", PICTURE_QUALITY, "-f", "mpeg" },
                     to, progress, cancel, error);
}

bool extractAudio(const std::string& ffmpeg, const std::string& from, const std::string& to, std::atomic<float>* progress,
                  const std::atomic<bool>& cancel, std::string& error){
    if (runFfmpeg(ffmpeg, from, { "-vn", "-c:a", "libmp3lame", "-q:a", SOUND_QUALITY, "-f", "mp3" }, to, progress, cancel, error)) return true;
    if (error == "nothing of that kind in it") error = "the video has no sound";
    return false;
}
