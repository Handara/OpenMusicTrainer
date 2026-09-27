#include "learn/lessonview.h"

#include "audio/audio.h"
#include "imgui.h"
#include "rlImGui.h"

#include <algorithm>
#include <filesystem>

namespace fs = std::filesystem;

const float HEADING_SCALE = 1.5f;       // a step's title, compared with body text
const float MAX_IMAGE_HEIGHT = 0.55f;   // of the window's height, so the caption and buttons stay in view
const ImU32 TEXT_DIM = IM_COL32(220, 200, 180, 200);

std::string lessonGoalText(const LessonStep& step, const ExerciseEntry* exercise){
    if (step.type == LessonStepType::Play) return "Goal: hit " + std::to_string(lessonGoal(step)) + "% of the notes";
    if (step.type != LessonStepType::Exercise || !exercise) return "";
    int goal = lessonGoal(step, exercise->exercise.type);
    if (exercise->exercise.type == ExerciseType::Scale) return "Goal: " + std::to_string(goal) + (goal == 1 ? " clean pass" : " clean passes");
    return "Goal: " + std::to_string(goal) + " right answers in a row";
}

void releaseLessonMedia(LessonMedia& media){
    if (!media.imagePath.empty()) UnloadTexture(media.texture);
    if (!media.audioPath.empty()) unloadSong();
    media = LessonMedia{};
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
    // As big as fits, keeping its shape: never wider than the step, nor taller than about half the window
    float maxHeight = ImGui::GetIO().DisplaySize.y * MAX_IMAGE_HEIGHT;
    float scale = std::min(width / media.texture.width, maxHeight / media.texture.height);
    float w = media.texture.width * scale, h = media.texture.height * scale;
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (width - w) / 2);
    rlImGuiImageSize(&media.texture, (int)w, (int)h);
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
        double position = songPosition(), length = songLength();
        ImGui::Text("%d:%02d / %d:%02d", (int)position / 60, (int)position % 60, (int)length / 60, (int)length % 60);
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
            ImGui::TextColored(ImColor(TEXT_DIM), "Video: %s (plays here once the video player is built)", step.file.c_str());
            break;
        case LessonStepType::Exercise:
            if (exercise) ImGui::Text("Exercise: %s", exercise->exercise.title.c_str());
            else ImGui::TextColored(ImColor(TEXT_DIM), "Exercise: %s (not found)", step.exercise.c_str());
            if (exercise && !exercise->exercise.description.empty()) ImGui::TextWrapped("%s", exercise->exercise.description.c_str());
            ImGui::TextColored(ImColor(TEXT_DIM), "%s", lessonGoalText(step, exercise).c_str());
            break;
        case LessonStepType::Play:
            ImGui::Text("Play along: %s", step.file.c_str());
            ImGui::TextColored(ImColor(TEXT_DIM), "%s", lessonGoalText(step, nullptr).c_str());
            break;
    }
    if (!step.caption.empty()){
        ImGui::Dummy(ImVec2(0, 4));
        ImGui::TextColored(ImColor(TEXT_DIM), "%s", step.caption.c_str());
    }
    if (!media.error.empty()) ImGui::TextColored(ImColor(255, 130, 110), "%s", media.error.c_str());
    ImGui::PopTextWrapPos();
}
