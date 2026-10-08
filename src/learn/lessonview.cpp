#include "learn/lessonview.h"

#include "audio/audio.h"
#include "imgui.h"
#include "rlImGui.h"
#include "video/video.h"
#include "ui/theme.h"

#include <algorithm>
#include <filesystem>

namespace fs = std::filesystem;

const float HEADING_SCALE = 1.5f;       // a step's title, compared with body text
const float MAX_IMAGE_HEIGHT = 0.55f;   // of the window's height, so the caption and buttons stay in view

void releaseLessonMedia(LessonMedia& media){
    if (!media.imagePath.empty()) UnloadTexture(media.texture);
    if (!media.audioPath.empty()) unloadSong();
    if (!media.videoPath.empty()) closeVideo();
    media = LessonMedia{};
}

// As big as fits, keeping its shape: never wider than the step, nor taller than about half the window
static void drawFitted(const Texture2D& texture, float width){
    float maxHeight = ImGui::GetIO().DisplaySize.y * MAX_IMAGE_HEIGHT;
    float scale = std::min(width / texture.width, maxHeight / texture.height);
    float w = texture.width * scale, h = texture.height * scale;
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (width - w) / 2);
    rlImGuiImageSize(&texture, (int)w, (int)h);
}

static void drawTime(double position, double length){
    ImGui::Text("%d:%02d / %d:%02d", (int)position / 60, (int)position % 60, (int)length / 60, (int)length % 60);
}

static void drawImage(const std::string& path, LessonMedia& media, float width){
    if (media.imagePath != path){
        if (!media.imagePath.empty()) UnloadTexture(media.texture);
        media.imagePath.clear();
        media.texture = LoadTexture(path.c_str());
        if (media.texture.id == 0){
            media.error = "Could not load " + fs::path(path).filename().string();
            return;
        }
        SetTextureFilter(media.texture, TEXTURE_FILTER_BILINEAR); // smooth when drawn at another size
        media.imagePath = path;
        media.error.clear();
    }
    drawFitted(media.texture, width);
}

// The first picture until Play; Stop closes it, and it opens again on its first picture
static void drawVideo(const std::string& path, LessonMedia& media, float width){
    if (media.videoPath != path){
        if (!media.videoPath.empty()) closeVideo();
        media.videoPath.clear();
        if (!media.audioPath.empty()){ unloadSong(); media.audioPath.clear(); } // the video's sound needs the song stream
        std::string error;
        if (!openVideo(path, error)){
            media.error = error;
            return;
        }
        media.videoPath = path;
        media.error.clear();
    }
    drawFitted(videoTexture(), width);
    bool playing = videoPlaying();
    if (ImGui::Button(playing ? "Stop" : "Play", ImVec2(120, 0))){
        if (playing){
            closeVideo();
            media.videoPath.clear();
        } else {
            playVideo();
        }
    }
    ImGui::SameLine();
    drawTime(videoPosition(), videoLength());
}

static void drawAudio(const std::string& path, LessonMedia& media){
    bool loaded = media.audioPath == path;
    bool playing = loaded && !songEnded();
    if (ImGui::Button(playing ? "Stop" : "Play", ImVec2(120, 0))){
        if (playing){
            unloadSong();
            media.audioPath.clear();
        } else {
            if (!media.audioPath.empty()) unloadSong(); // another clip was loaded
            media.audioPath.clear();
            std::string error;
            if (loadSong(path, error)){
                playSong(false);
                media.audioPath = path;
                media.error.clear();
            } else {
                media.error = error;
            }
        }
    }
    if (media.audioPath == path){
        ImGui::SameLine();
        drawTime(songPosition(), songLength());
    }
}

void drawLessonStep(const LessonStep& step, const std::string& folder, const ExerciseEntry* exercise, LessonMedia& media, float width){
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + width);
    if (!step.title.empty()){
        ImGui::PushFont(nullptr, ImGui::GetFontSize() * HEADING_SCALE); // same font, bigger
        ImGui::TextWrapped("%s", step.title.c_str());
        ImGui::PopFont();
        ImGui::Dummy(ImVec2(0, 8));
    }
    std::string path = (fs::path(folder) / step.file).string();

    switch (step.type){
        case LessonStepType::Text:
            for (const std::string& paragraph : step.paragraphs){
                ImGui::TextWrapped("%s", paragraph.c_str());
                ImGui::Dummy(ImVec2(0, 6));
            }
            break;
        case LessonStepType::Image:
            if (!step.file.empty()) drawImage(path, media, width);
            break;
        case LessonStepType::Audio:
            if (!step.file.empty()) drawAudio(path, media);
            break;
        case LessonStepType::Video:
            if (!step.file.empty()) drawVideo(path, media, width);
            break;
        case LessonStepType::Exercise:
            if (exercise) ImGui::Text("Exercise: %s", exercise->exercise.title.c_str());
            else ImGui::TextColored(ImColor(uiColor(UiColor::Dim)), "Exercise: %s (not found)", step.exercise.c_str());
            if (exercise && !exercise->exercise.description.empty()) ImGui::TextWrapped("%s", exercise->exercise.description.c_str());
            ImGui::TextColored(ImColor(uiColor(UiColor::Dim)), "%s", lessonGoalText(step, exercise).c_str());
            break;
        case LessonStepType::Play:
            ImGui::Text("Play along: %s", step.file.c_str());
            ImGui::TextColored(ImColor(uiColor(UiColor::Dim)), "%s", lessonGoalText(step, nullptr).c_str());
            break;
    }
    if (!step.caption.empty()){
        ImGui::Dummy(ImVec2(0, 4));
        ImGui::TextColored(ImColor(uiColor(UiColor::Dim)), "%s", step.caption.c_str());
    }
    if (!media.error.empty()) ImGui::TextColored(ImColor(uiColor(UiColor::Bad)), "%s", media.error.c_str());
    ImGui::PopTextWrapPos();
}
