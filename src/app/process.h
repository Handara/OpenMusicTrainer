#pragma once

#include <atomic>
#include <functional>
#include <string>
#include <vector>

// Running another program (FFmpeg, from the video add-on) and reading what it prints, with no window of its own.
// `arguments`: the program first, then what it's given; each is passed whole, spaces and quotes and all, with no
// shell in between to read them another way. `onLine` gets each line it prints (both of its outputs, as they come),
// on the calling thread. It holds the caller until the program ends: for a worker thread. `cancel` stops the
// program. True if it ran and ended well (with 0).
bool runProgram(const std::vector<std::string>& arguments, const std::function<void(const std::string&)>& onLine,
                const std::atomic<bool>& cancel, std::string& error);

// A program by name ("ffmpeg"), where the system looks for programs (its PATH): its full path, "" if it isn't there
std::string findProgram(const std::string& name);
