#include "screens/newsong.h"

#include "audio/audio.h"
#include "core/songlibrary.h"
#include "imgui.h"
#include "imgui_stdlib.h"
#include "raylib.h"
#include "ui/menulist.h"
#include "ui/theme.h"
#include "ui/ui.h"

#include <filesystem>

namespace fs = std::filesystem;

static struct {
    std::string songsDir;
    std::string audioPath;
    std::string title;
    std::string artist;
    double bpm = 120.0;
    bool titleFromFile = true; // the title is still the file's name, so a new file renames it
    std::string error;
    std::string chartPath;     // the song made
} form;

void openNewSongScreen(const std::string& userSongsDir){
    form = {};
    form.songsDir = userSongsDir;
}

std::string newSongChartPath(){
    return form.chartPath;
}

// A new audio file: its name becomes the title unless one was typed
static void audioChosen(){
    if (form.titleFromFile) form.title = fs::path(form.audioPath).stem().string();
    form.error.clear();
}

static bool create(){
    // The chart must cover the whole audio, so it's measured first; loading it also proves the game can read it
    std::string error;
    if (!loadSong(form.audioPath, error)){
        form.error = "Can't read this audio (.wav, .ogg, .mp3 and .flac work): " + error;
        return false;
    }
    NewSong song;
    song.audioPath = form.audioPath;
    song.title = form.title;
    song.artist = form.artist;
    song.bpm = form.bpm;
    song.lengthSeconds = songLength();
    unloadSong();
    if (!createSong(form.songsDir, song, form.chartPath, error)){
        form.error = error;
        return false;
    }
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
    field("Audio file");
    if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
    if (ImGui::InputTextWithHint("##audio", "Drop a file on the window, or type its path", &form.audioPath,
                                 ImGuiInputTextFlags_EnterReturnsTrue)) enter = true;
    if (ImGui::IsItemEdited()) audioChosen();
    ImGui::Dummy(ImVec2(0, 8 * s));
    field("Title");
    if (ImGui::InputText("##title", &form.title, ImGuiInputTextFlags_EnterReturnsTrue)) enter = true;
    if (ImGui::IsItemEdited()) form.titleFromFile = false;
    ImGui::Dummy(ImVec2(0, 8 * s));
    field("Artist");
    if (ImGui::InputText("##artist", &form.artist, ImGuiInputTextFlags_EnterReturnsTrue)) enter = true;
    ImGui::Dummy(ImVec2(0, 8 * s));
    field("Tempo (BPM): set it closer in the editor, with the metronome");
    if (ImGui::InputDouble("##bpm", &form.bpm, 1.0, 10.0, "%.2f", ImGuiInputTextFlags_EnterReturnsTrue)) enter = true;

    ImGui::Dummy(ImVec2(0, 16 * s));
    ImGui::SetCursorPosX(left);
    if ((ImGui::Button("Create") || enter) && create()) choice = NewSongChoice::Created;
    ImGui::SameLine();
    if (ImGui::Button("Back")) choice = NewSongChoice::Back;
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
