#include "app/screenrecorder.h"

#include "app/process.h"
#include "raylib.h"
#include "rlgl.h"

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstdlib>
#include <deque>
#include <mutex>
#include <thread>

const int PICTURES_PER_SECOND = 30;
const size_t MOST_WAITING = 8;  // pictures waiting for FFmpeg: more, and the newest is left out (FFmpeg is behind)
const int MOST_REPEATED = 15;   // a frame that took long stands in for the pictures missed, up to half a second

static struct {
    FedProgram* ffmpeg = nullptr;
    std::thread writer;           // gives FFmpeg the pictures, so the game never waits on it
    std::mutex lock;
    std::condition_variable woken;
    std::deque<std::vector<unsigned char>> waiting;
    bool running = false;
    std::atomic<bool> failed{false};
    int width = 0, height = 0;
    double startedAt = 0.0;
    long long taken = 0;          // pictures given so far, counting the repeated ones
} recorder;

static void giveToFfmpeg(){
    while (true){
        std::vector<unsigned char> picture;
        {
            std::unique_lock<std::mutex> hold(recorder.lock);
            recorder.woken.wait(hold, []{ return !recorder.waiting.empty() || !recorder.running; });
            if (recorder.waiting.empty()) return; // stopped, and all given
            picture = std::move(recorder.waiting.front());
            recorder.waiting.pop_front();
        }
        if (!recorder.failed && !feedProgram(recorder.ffmpeg, picture.data(), picture.size())) recorder.failed = true;
    }
}

bool startScreenRecording(const std::string& ffmpeg, const std::string& path, std::string& error){
    if (recorder.running) return true;
    recorder.width = GetRenderWidth() / 2 * 2; // even: what H.264 takes
    recorder.height = GetRenderHeight() / 2 * 2;
    const std::string size = std::to_string(recorder.width) + "x" + std::to_string(recorder.height);
    recorder.ffmpeg = startFedProgram({ ffmpeg, "-y", "-loglevel", "error", "-f", "rawvideo", "-pix_fmt", "rgba", "-s", size,
                                        "-framerate", std::to_string(PICTURES_PER_SECOND), "-i", "-",
                                        "-vf", "scale=-2:'min(720,ih)'", "-c:v", "libx264", "-preset", "veryfast", "-crf", "23",
                                        "-pix_fmt", "yuv420p", "-movflags", "+faststart", path }, error);
    if (!recorder.ffmpeg) return false;
    recorder.running = true;
    recorder.failed = false;
    recorder.taken = 0;
    recorder.startedAt = GetTime();
    recorder.writer = std::thread(giveToFfmpeg);
    return true;
}

bool screenRecording(){
    return recorder.running;
}

void captureScreen(){
    if (!recorder.running) return;
    const long long due = (long long)((GetTime() - recorder.startedAt) * PICTURES_PER_SECOND) + 1;
    if (recorder.taken >= due) return;
    // The screen as it's drawn, top row first (raylib turns OpenGL's rows the right way up)
    unsigned char* pixels = rlReadScreenPixels(recorder.width, recorder.height);
    if (!pixels) return;
    std::vector<unsigned char> picture(pixels, pixels + (size_t)recorder.width * recorder.height * 4);
    RL_FREE(pixels);
    std::lock_guard<std::mutex> hold(recorder.lock);
    // A slow frame: it stands in for the pictures that weren't taken, so the video keeps the clock's time
    const long long count = std::min<long long>(due - recorder.taken, MOST_REPEATED);
    for (long long i = 0; i < count && recorder.waiting.size() < MOST_WAITING; i++) recorder.waiting.push_back(picture);
    recorder.taken = due;
    recorder.woken.notify_one();
}

bool stopScreenRecording(std::string& error){
    if (!recorder.running) return false;
    {
        std::lock_guard<std::mutex> hold(recorder.lock);
        recorder.running = false;
    }
    recorder.woken.notify_one();
    recorder.writer.join();
    const bool finished = finishFedProgram(recorder.ffmpeg, error);
    recorder.ffmpeg = nullptr;
    if (recorder.failed && error.empty()) error = "FFmpeg stopped taking the pictures";
    return finished && !recorder.failed;
}

bool addSoundToVideo(const std::string& ffmpeg, const std::string& video, const std::vector<VideoSound>& sounds,
                     const std::string& to, std::string& error){
    std::vector<std::string> arguments = { ffmpeg, "-y", "-loglevel", "error", "-i", video };
    std::string filter, mixed;
    for (size_t i = 0; i < sounds.size(); i++){
        const VideoSound& sound = sounds[i];
        arguments.insert(arguments.end(), { "-i", sound.path });
        const std::string in = "[" + std::to_string(i + 1) + ":a]", out = "[s" + std::to_string(i) + "]";
        // Later into the video: silence before it; earlier: its first seconds cut
        std::string place = sound.at >= 0.0 ? "adelay=" + std::to_string((long long)(sound.at * 1000.0)) + ":all=1"
                                             : "atrim=start=" + std::to_string(-sound.at) + ",asetpts=PTS-STARTPTS";
        filter += in + place + ",volume=" + std::to_string(sound.volume) + out + ";";
        mixed += out;
    }
    filter += mixed + "amix=inputs=" + std::to_string(sounds.size()) + ":normalize=0:duration=longest,aformat=channel_layouts=stereo[sound]";
    arguments.insert(arguments.end(), { "-filter_complex", filter, "-map", "0:v", "-map", "[sound]", "-c:v", "copy",
                                        "-c:a", "aac", "-b:a", "192k", "-shortest", "-movflags", "+faststart", to });
    std::atomic<bool> cancel{false};
    std::string printed;
    if (runProgram(arguments, [&](const std::string& line){ printed += line + "\n"; }, cancel, error)) return true;
    if (error.empty()) error = printed.empty() ? "FFmpeg couldn't put the sound under the video" : printed;
    return false;
}
