#include "app/gamerecorder.h"

#include "app/screenrecorder.h"
#include "app/videoconvert.h"
#include "audio/audio.h"
#include "imgui.h"
#include "raylib.h"
#include "rlgl.h"
#include "ui/theme.h"
#include "ui/ui.h"
#include "views/viewfont.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <thread>
#include <vector>

namespace fs = std::filesystem;

const double NOTICE_SECONDS = 6.0;

namespace {
// A WAV file written as it goes: 16-bit stereo, its sizes filled in when it's closed
struct WavWriter {
    std::ofstream file;
    uint32_t frames = 0;

    void put(uint32_t value, int bytes){
        for (int i = 0; i < bytes; i++) file.put((char)((value >> (8 * i)) & 0xff)); // little-endian, as WAV is
    }
    void header(int rate){
        const uint32_t data = frames * 4;
        file.write("RIFF", 4); put(36 + data, 4); file.write("WAVEfmt ", 8);
        put(16, 4); put(1, 2); put(2, 2); put((uint32_t)rate, 4); put((uint32_t)rate * 4, 4); put(4, 2); put(16, 2);
        file.write("data", 4); put(data, 4);
    }
    bool open(const std::string& path){
        frames = 0;
        file.open(path, std::ios::binary | std::ios::trunc);
        header(0); // its sizes and rate once they're known
        return (bool)file;
    }
    void write(const std::vector<float>& stereo){
        for (float sample : stereo){
            const int value = (int)std::lround(std::clamp(sample, -1.0f, 1.0f) * 32767.0f);
            put((uint32_t)(uint16_t)(int16_t)value, 2);
        }
        frames += (uint32_t)(stereo.size() / 2);
    }
    void close(int rate){
        file.seekp(0);
        header(rate);
        file.close();
    }
};
}

static struct {
    std::string dataDir;
    bool recording = false;
    double startedAt = 0.0;
    std::string base;          // the recording's files, without their endings
    std::string ffmpeg;
    WavWriter sound;
    std::vector<float> taken;  // what went out, taken this frame
    std::thread saver;         // puts the sound under the pictures
    std::atomic<bool> saving{false};
    bool shotDue = false;      // F12 this frame
    std::mutex noticeLock;     // (the saver sets it too)
    std::string notice;        // what was just done
    bool noticeFolder = false; // with the folder to open
    double noticeAt = -100.0;
} gameRecorder;

static std::string recordingsFolder(){
    return (fs::path(gameRecorder.dataDir) / "recordings").string();
}

// The moment, for a file's name: lahn-2026-10-08-221530
static std::string stampedName(){
    char stamp[32];
    std::time_t clock = std::time(nullptr);
    std::strftime(stamp, sizeof stamp, "%Y-%m-%d-%H%M%S", std::localtime(&clock));
    return std::string("lahn-") + stamp;
}

static void setNotice(const std::string& text, bool folder){
    std::lock_guard<std::mutex> hold(gameRecorder.noticeLock);
    gameRecorder.notice = text;
    gameRecorder.noticeFolder = folder;
    gameRecorder.noticeAt = GetTime();
}

void initGameRecorder(const std::string& userDataDir){
    gameRecorder.dataDir = userDataDir;
}

// What went out since the last frame, written down
static void keepSound(){
    takeOutputRecording(gameRecorder.taken);
    gameRecorder.sound.write(gameRecorder.taken);
    gameRecorder.taken.clear();
}

static void startRecording(){
    if (gameRecorder.saving){
        setNotice("The last recording is still being saved: a moment", false);
        return;
    }
    if (screenRecording()){
        setNotice("A check is being recorded (F9): the game can be once it's done", false);
        return;
    }
    gameRecorder.ffmpeg = findFfmpeg(gameRecorder.dataDir + "/addons");
    if (gameRecorder.ffmpeg.empty()){
        setNotice("Recording needs FFmpeg: drop lahn's video add-on (lahn-video...lahnaddon) on the song list, or install FFmpeg", false);
        return;
    }
    std::error_code ec;
    fs::create_directories(recordingsFolder(), ec);
    gameRecorder.base = (fs::path(recordingsFolder()) / stampedName()).string();
    // As smooth as the game: 60 pictures a second, up to 1080 lines, close to what was drawn
    ScreenRecordingOptions options;
    options.picturesPerSecond = 60;
    options.mostLines = 1080;
    options.quality = 20;
    std::string error;
    if (!startScreenRecording(gameRecorder.ffmpeg, gameRecorder.base + "-pictures.mp4", error, options)){
        setNotice("Couldn't record: " + error, false);
        return;
    }
    if (!gameRecorder.sound.open(gameRecorder.base + "-sound.wav")){
        std::string ignored;
        stopScreenRecording(ignored);
        fs::remove(gameRecorder.base + "-pictures.mp4", ec);
        setNotice("Couldn't record: nothing can be written in " + recordingsFolder(), false);
        return;
    }
    startOutputRecording();
    gameRecorder.recording = true;
    gameRecorder.startedAt = GetTime();
    setNotice("", false);
}

static void stopRecording(){
    gameRecorder.recording = false;
    stopOutputRecording();
    keepSound(); // the last of it
    const bool heard = gameRecorder.sound.frames > 0;
    gameRecorder.sound.close(outputRecordingRate() > 0 ? outputRecordingRate() : 48000);
    std::string error;
    const std::string base = gameRecorder.base, pictures = base + "-pictures.mp4", sound = base + "-sound.wav";
    if (!stopScreenRecording(error)){
        setNotice("The recording couldn't be saved: " + error, false);
        return;
    }
    // The sound under the pictures, on a thread (a long recording takes a while). What goes out is heard a little
    // later, and the picture shows what's heard: the sound goes that much later.
    if (gameRecorder.saver.joinable()) gameRecorder.saver.join();
    gameRecorder.saving = true;
    setNotice("Saving the recording...", false);
    const double late = outputLatencySeconds();
    gameRecorder.saver = std::thread([ffmpeg = gameRecorder.ffmpeg, base, pictures, sound, heard, late]{
        std::string error;
        std::error_code ignored;
        const std::string video = base + ".mp4";
        bool saved;
        if (heard){
            saved = addSoundToVideo(ffmpeg, pictures, { { sound, late, 1.0f } }, video, error);
        } else { // nothing went out (no output device): the pictures alone
            fs::rename(pictures, video, ignored);
            saved = !ignored;
        }
        if (saved){
            fs::remove(pictures, ignored);
            fs::remove(sound, ignored);
            setNotice("Recording saved: " + fs::path(video).filename().string(), true);
        } else {
            setNotice("The sound couldn't be put under the recording (its pictures and sound are kept): " + error, true);
        }
        gameRecorder.saving = false;
    });
}

void updateGameRecorder(){
    if (gameRecorder.recording) keepSound();
}

void gameRecorderUi(float scale){
    gameRecorder.shotDue = ImGui::IsKeyPressed(ImGuiKey_F12, false);
    if (ImGui::IsKeyPressed(ImGuiKey_F10, false)){
        if (gameRecorder.recording) stopRecording();
        else startRecording();
    }
    if (gameRecorder.recording || gameRecorder.shotDue) return; // nothing over what's being recorded
    std::string text;
    bool folder;
    double at;
    {
        std::lock_guard<std::mutex> hold(gameRecorder.noticeLock);
        text = gameRecorder.notice;
        folder = gameRecorder.noticeFolder;
        at = gameRecorder.noticeAt;
    }
    const double age = GetTime() - at;
    if (text.empty() || (!gameRecorder.saving && age > NOTICE_SECONDS)) return;
    const float s = scale;
    const ImGuiIO& io = ImGui::GetIO();
    // At the bottom, in the middle, fading out at the end
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x / 2, io.DisplaySize.y - 64 * s), ImGuiCond_Always, ImVec2(0.5f, 1.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, (float)std::clamp((NOTICE_SECONDS - age) / 0.5, 0.0, 1.0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 18 * s);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(18 * s, 10 * s));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, uiColorVec(UiColor::Card));
    ImGui::PushStyleColor(ImGuiCol_Border, uiColorVec(UiColor::StaffLine));
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings
                                   | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove;
    if (ImGui::Begin("##recorder notice", nullptr, flags)){
        ImGui::PushFont(uiFonts().text, 15 * s);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(text.c_str());
        if (folder){
            ImGui::SameLine(0, 14 * s);
            if (ImGui::Button("Open the folder")) openFolder(recordingsFolder());
        }
        ImGui::PopFont();
    }
    ImGui::End();
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(4);
}

void captureGameRecorder(){
    if (!gameRecorder.shotDue) return;
    gameRecorder.shotDue = false;
    std::error_code ec;
    fs::create_directories(recordingsFolder(), ec);
    const std::string path = (fs::path(recordingsFolder()) / (stampedName() + ".png")).string();
    rlDrawRenderBatchActive(); // everything drawn so far in the picture
    Image shot = LoadImageFromScreen();
    const bool saved = shot.data && ExportImage(shot, path.c_str());
    UnloadImage(shot);
    setNotice(saved ? "Screenshot saved: " + fs::path(path).filename().string() : "Couldn't save the screenshot in " + recordingsFolder(), saved);
}

void drawGameRecorderOverlay(float scale){
    if (!gameRecorder.recording) return;
    const float s = scale;
    const int seconds = (int)(GetTime() - gameRecorder.startedAt);
    const char* label = TextFormat("REC  %d:%02d", seconds / 60, seconds % 60);
    const float size = 14 * s, dot = 5 * s, padding = 12 * s;
    const float width = padding + 2 * dot + 8 * s + viewTextWidth(label, size) + padding, height = 28 * s;
    const float x = GetScreenWidth() / 2.0f - width / 2, y = 10 * s;
    DrawRectangleRounded({ x, y, width, height }, 1.0f, 12, Fade(themeColor(UiColor::Card), 0.92f));
    const float pulse = reducedMotion() ? 1.0f : 0.65f + 0.35f * std::sin((float)GetTime() * 4.0f);
    DrawCircleV({ x + padding + dot, y + height / 2 }, dot, Fade(themeColor(UiColor::Bad), pulse));
    drawViewText(label, x + padding + 2 * dot + 8 * s, y + height / 2, size, themeColor(UiColor::Ink), 0.0f);
}

void closeGameRecorder(){
    if (gameRecorder.recording) stopRecording();
    if (gameRecorder.saver.joinable()) gameRecorder.saver.join();
}
