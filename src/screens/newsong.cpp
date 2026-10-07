#include "screens/newsong.h"

#include "app/filedialog.h"
#include "app/videoconvert.h"
#include "audio/audio.h"
#include "core/songlibrary.h"
#include "imgui.h"
#include "imgui_stdlib.h"
#include "raylib.h"
#include "ui/menulist.h"
#include "ui/theme.h"
#include "ui/ui.h"

#include <atomic>
#include <filesystem>

namespace fs = std::filesystem;

static struct {
    std::string songsDir;
    std::string addonsDir;
    std::string audioPath;     // the song's audio, or a video whose sound it is
    std::string title;
    std::string artist;
    double bpm = 120.0;
    bool findTempo = true;     // the editor listens to the song for its tempo and beats as it opens
    bool titleFromFile = true; // the title is still the file's name, so a new file renames it
    std::string error;
    std::string chartPath;     // the song made
    std::string videoPath;     // and the video it was made from, for its pictures to be brought in next
    int creating = 0;          // frames since Create was pressed on a video: its sound takes a few seconds to read
} form;

// A new audio file: its name becomes the title unless one was typed
static void audioChosen();

void openNewSongScreen(const std::string& userSongsDir, const std::string& addonsDir, const std::string& audioPath){
    form = {};
    form.songsDir = userSongsDir;
    form.addonsDir = addonsDir;
    if (!audioPath.empty()){
        form.audioPath = audioPath;
        audioChosen();
    }
}

std::string newSongChartPath(){
    return form.chartPath;
}

std::string newSongVideoPath(){
    return form.videoPath;
}

bool newSongFindsTempo(){
    return form.findTempo;
}

static void audioChosen(){
    if (form.titleFromFile) form.title = fs::path(form.audioPath).stem().string();
    form.error.clear();
}

static bool create(){
    std::string error;
    std::string audioPath = form.audioPath;
    form.videoPath.clear();
    // From a video: its sound is the song's audio, read out of it first (app/videoconvert); its pictures are brought
    // in once the song exists, in the editor
    const bool fromVideo = isVideoFile(form.audioPath);
    if (fromVideo){
        const std::string ffmpeg = findFfmpeg(form.addonsDir);
        if (ffmpeg.empty()){
            form.error = "A song from a video needs lahn's video add-on: drop its file (lahn-video...lahnaddon) on the song list first";
            return false;
        }
        audioPath = (fs::temp_directory_path() / "lahn-video-sound.mp3").string();
        std::atomic<bool> cancel{false};
        if (!extractAudio(ffmpeg, form.audioPath, audioPath, nullptr, cancel, error)){
            form.error = fs::path(form.audioPath).filename().string() + ": " + error
                         + (error == "the video has no sound" ? ". Make the song from its audio file, then bring the video in from the editor (Import)" : "");
            return false;
        }
    }
    // The chart must cover the whole audio, so it's measured first; loading it also proves the game can read it
    if (!loadSong(audioPath, error)){
        form.error = "Can't read this audio (.wav, .ogg, .mp3 and .flac work): " + error;
        return false;
    }
    NewSong song;
    song.audioPath = audioPath;
    song.title = form.title;
    song.artist = form.artist;
    song.bpm = form.bpm;
    song.lengthSeconds = songLength();
    unloadSong();
    bool made = createSong(form.songsDir, song, form.chartPath, error);
    std::error_code ec;
    if (fromVideo) fs::remove(audioPath, ec); // it was copied into the song
    if (!made){
        form.error = error;
        return false;
    }
    if (fromVideo) form.videoPath = form.audioPath;
    return true;
}

NewSongChoice newSongScreen(){
    NewSongChoice choice = NewSongChoice::None;
    beginMenu("New song");
    menuTitle("New song");
    float s = menuScale(), width = ImGui::GetWindowWidth();
    float left = width * 0.07f, fieldWidth = width * 0.5f;

    // A file dropped anywhere on the window is the audio
    if (IsFileDropped()){
        FilePathList dropped = LoadDroppedFiles();
        if (dropped.count > 0){
            form.audioPath = dropped.paths[0];
            audioChosen();
        }
        UnloadDroppedFiles(dropped);
    }

    bool enter = false;
    auto field = [&](const char* label){
        ImGui::SetCursorPosX(left);
        ImGui::TextColored(uiColorVec(UiColor::Dim), "%s", label);
        ImGui::SetCursorPosX(left);
        ImGui::SetNextItemWidth(fieldWidth);
    };
    field("Audio file, or a video");
    if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
    if (ImGui::InputTextWithHint("##audio", "Drop a file on the window, choose it, or type its path", &form.audioPath,
                                 ImGuiInputTextFlags_EnterReturnsTrue)) enter = true;
    if (ImGui::IsItemEdited()) audioChosen();
    // The system's Open dialog, beside it
    ImGui::SameLine();
    if (ImGui::Button("Choose...")){
        std::string path, error;
        if (chooseFile("Choose the song's audio, or its video", "Audio and video", { "*.mp3", "*.ogg", "*.flac", "*.wav", "*.mp4", "*.m4v", "*.mkv", "*.webm", "*.mov", "*.avi", "*.wmv", "*.flv" },
                       path, error)){
            form.audioPath = path;
            audioChosen();
        } else if (!error.empty()) form.error = error;
    }
    ImGui::Dummy(ImVec2(0, 8 * s));
    field("Title");
    if (ImGui::InputText("##title", &form.title, ImGuiInputTextFlags_EnterReturnsTrue)) enter = true;
    if (ImGui::IsItemEdited()) form.titleFromFile = false;
    ImGui::Dummy(ImVec2(0, 8 * s));
    field("Artist");
    if (ImGui::InputText("##artist", &form.artist, ImGuiInputTextFlags_EnterReturnsTrue)) enter = true;
    ImGui::Dummy(ImVec2(0, 8 * s));
    ImGui::SetCursorPosX(left);
    ImGui::Checkbox("Find the tempo and the bars from the song", &form.findTempo);
    if (!form.findTempo){
        field("Tempo (BPM): set it closer in the editor, with the metronome");
        if (ImGui::InputDouble("##bpm", &form.bpm, 1.0, 10.0, "%.2f", ImGuiInputTextFlags_EnterReturnsTrue)) enter = true;
    } else {
        ImGui::SetCursorPosX(left);
        ImGui::PushTextWrapPos(left + fieldWidth);
        ImGui::TextColored(uiColorVec(UiColor::Dim), "As the editor opens, the song is listened to for its beats and its bars are laid on them. Details has what to do if they start on the wrong beat.");
        ImGui::PopTextWrapPos();
    }

    ImGui::Dummy(ImVec2(0, 16 * s));
    ImGui::SetCursorPosX(left);
    // A video's sound takes a few seconds to read out of it: the button says so for a frame before the work starts
    const bool video = isVideoFile(form.audioPath);
    if ((ImGui::Button(form.creating > 0 ? "Reading the video's sound..." : "Create") || enter) && form.creating == 0){
        if (video) form.creating = 1;
        else if (create()) choice = NewSongChoice::Created;
    }
    if (form.creating > 0 && ++form.creating > 3){
        form.creating = 0;
        if (create()) choice = NewSongChoice::Created;
    }
    if (video && form.error.empty()){
        ImGui::SetCursorPosX(left);
        ImGui::PushTextWrapPos(left + fieldWidth);
        ImGui::TextColored(uiColorVec(UiColor::Dim), "A video: its sound becomes the song's audio, and its pictures play behind the notes (they're brought in next, in the editor).");
        ImGui::PopTextWrapPos();
    }
    if (!form.error.empty()){
        ImGui::SetCursorPosX(left);
        ImGui::PushTextWrapPos(left + fieldWidth);
        ImGui::TextColored(uiColorVec(UiColor::Bad), "%s", form.error.c_str());
        ImGui::PopTextWrapPos();
    }
    menuScreenHint("Enter  create    Esc  back", s);
    ImGui::End();
    return choice;
}
