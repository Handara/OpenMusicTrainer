#include "screens/lessoneditor.h"

#include "core/lesson.h"
#include "imgui.h"
#include "imgui_stdlib.h"
#include "learn/lessonview.h"
#include "raylib.h"
#include "ui/ui.h"
#include "ui/theme.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <sstream>

namespace fs = std::filesystem;

const float STEPS_PANEL_WIDTH = 330.0f;
const float FIELDS_PANEL_WIDTH = 400.0f;
const char* const UNSAVED_POPUP = "Unsaved changes";
const char* const STEP_TYPE_LABELS[] = { "Text", "Image", "Audio", "Video", "Exercise", "Play" }; // LessonStepType order
// Lessons play video in MPEG-1 (see the video player): the command that converts any video to it
const char* const VIDEO_CONVERT_HINT = "Videos must be MPEG-1 (.mpg). To convert one with FFmpeg:\n"
                                       "ffmpeg -i video.mp4 -c:v mpeg1video -q:v 4 -c:a mp2 -b:a 192k video.mpg";

static struct {
    LessonEditorSetup setup;
    std::vector<LessonEntry> lessons;      // the list to pick from
    std::vector<ExerciseEntry> exercises;  // for exercise steps
    std::string newLessonName;
    std::string listError;

    // The lesson being edited (editing = true)
    bool editing = false;
    Lesson lesson;
    std::string folder;
    bool builtIn = false;
    bool dirty = false;
    std::string status;                    // the last save's result, or a hint
    int selected = 0;                      // the step being edited
    std::string paragraphs;                // the selected text step's paragraphs, one per line, as typed
    std::vector<std::string> folderFiles;  // the lesson folder's files, listed when a lesson opens or on Refresh
    int addType = 0;                       // the step type the Add button adds
    LessonMedia media;

    bool active = false;
} ed;

// --- Lists ----------------------------------------------------------------------------------------------------

static void refreshLists(){
    ed.exercises = scanExercises(ed.setup.builtInExercises, true);
    std::vector<ExerciseEntry> user = scanExercises(ed.setup.userExercises, false);
    ed.exercises.insert(ed.exercises.end(), user.begin(), user.end());
    checkRoutines(ed.exercises);

    ed.lessons = scanLessons(ed.setup.builtInLessons, true);
    std::vector<LessonEntry> userLessons = scanLessons(ed.setup.userLessons, false);
    ed.lessons.insert(ed.lessons.end(), userLessons.begin(), userLessons.end());
    checkLessonExercises(ed.lessons, ed.exercises);
}

static void refreshFolderFiles(){
    ed.folderFiles.clear();
    std::error_code ec;
    for (const fs::directory_entry& file : fs::directory_iterator(ed.folder, ec)){
        if (file.is_regular_file() && file.path().filename() != LESSON_FILE_NAME) ed.folderFiles.push_back(file.path().filename().string());
    }
    std::sort(ed.folderFiles.begin(), ed.folderFiles.end());
}

// --- Editing --------------------------------------------------------------------------------------------------

static LessonStep& selectedStep(){
    return ed.lesson.steps[ed.selected];
}

static void selectStep(int index){
    releaseLessonMedia(ed.media); // the old step's picture and sound
    ed.selected = std::clamp(index, 0, (int)ed.lesson.steps.size() - 1);
    std::string joined;
    for (const std::string& paragraph : selectedStep().paragraphs) joined += (joined.empty() ? "" : "\n") + paragraph;
    ed.paragraphs = joined;
}

static void openLesson(const std::string& folder, bool builtIn){
    std::string error;
    Lesson lesson;
    if (!loadLesson(folder, lesson, error)){
        ed.listError = error;
        return;
    }
    ed.lesson = lesson;
    ed.folder = folder;
    ed.builtIn = builtIn;
    ed.dirty = false;
    ed.status = builtIn ? "Built-in lesson: saving creates your own copy" : "";
    ed.editing = true;
    refreshFolderFiles();
    selectStep(0);
}

// A folder name from what the author typed: letters, digits, spaces, - and _ only, so it works on every system
static std::string folderNameFor(const std::string& name){
    std::string folder;
    for (char c : name) if (std::isalnum((unsigned char)c) || c == ' ' || c == '-' || c == '_') folder += c;
    while (!folder.empty() && folder.back() == ' ') folder.pop_back();
    return folder;
}

static void createLesson(){
    std::string folderName = folderNameFor(ed.newLessonName);
    if (folderName.empty()){
        ed.listError = "Give the lesson a name (letters, digits, spaces, - and _)";
        return;
    }
    fs::path folder = fs::path(ed.setup.userLessons) / folderName;
    if (fs::exists(folder)){
        ed.listError = "There's already a lesson folder called '" + folderName + "'";
        return;
    }
    Lesson lesson;
    lesson.title = ed.newLessonName;
    LessonStep first;
    first.title = "Welcome";
    first.paragraphs = {"What this lesson is about."};
    lesson.steps = {first};
    std::string error;
    if (!saveLesson(folder.string(), lesson, error)){
        ed.listError = error;
        return;
    }
    ed.newLessonName.clear();
    ed.listError.clear();
    refreshLists();
    openLesson(folder.string(), false);
}

static bool saveEditedLesson(){
    if (ed.builtIn){
        // Built-in lessons ship with the game and are read-only, so the first save copies the folder, media and all
        fs::path source = ed.folder;
        fs::path destination = fs::path(ed.setup.userLessons) / source.filename();
        for (int n = 2; fs::exists(destination); n++){
            destination = fs::path(ed.setup.userLessons) / (source.filename().string() + " (" + std::to_string(n) + ")");
        }
        std::error_code ec;
        fs::create_directories(destination.parent_path(), ec);
        if (!ec) fs::copy(source, destination, fs::copy_options::recursive, ec);
        if (ec){
            ed.status = "Could not copy the lesson: " + ec.message();
            return false;
        }
        ed.folder = destination.string();
        ed.builtIn = false;
    }
    std::string error;
    if (!saveLesson(ed.folder, ed.lesson, error)){
        ed.status = error;
        return false;
    }
    ed.dirty = false;

    // Read it back the way players will: the author sees any problem (a missing file, an unknown exercise) at once
    std::vector<LessonEntry> check(1);
    check[0].folder = ed.folder;
    check[0].builtIn = false;
    if (!loadLesson(ed.folder, check[0].lesson, check[0].error)) ed.status = "Saved, but it can't be played yet: " + check[0].error;
    else {
        checkLessonExercises(check, ed.exercises);
        ed.status = check[0].error.empty() ? "Saved to your lessons: " + fs::path(ed.folder).filename().string()
                                           : "Saved, but it can't be played yet: " + check[0].error;
    }
    return true;
}

static const ExerciseEntry* stepExercise(const LessonStep& step){
    if (step.type != LessonStepType::Exercise) return nullptr;
    return findExercise(ed.exercises, ed.builtIn, step.exercise);
}

// A one-line summary for the step list: "3. Image: em.png"
static std::string stepLabel(int index, const LessonStep& step){
    std::string what = step.title;
    if (what.empty()) what = step.type == LessonStepType::Exercise ? step.exercise : step.file;
    if (what.empty() && !step.paragraphs.empty()) what = step.paragraphs[0];
    if (what.size() > 28) what = what.substr(0, 26) + "...";
    return std::to_string(index + 1) + ". " + STEP_TYPE_LABELS[(int)step.type] + (what.empty() ? "" : ": " + what);
}

// --- Panels ---------------------------------------------------------------------------------------------------

static void stepsPanel(){
    ImGui::SeparatorText("Lesson");
    ImGui::PushItemWidth(-90);
    if (ImGui::InputText("Title", &ed.lesson.title)) ed.dirty = true;
    if (ImGui::InputText("Category", &ed.lesson.category)) ed.dirty = true;
    if (ImGui::InputText("Author", &ed.lesson.author)) ed.dirty = true;
    if (ImGui::InputText("About", &ed.lesson.description)) ed.dirty = true;
    ImGui::PopItemWidth();

    ImGui::SeparatorText("Steps");
    std::vector<LessonStep>& steps = ed.lesson.steps;
    for (int i = 0; i < (int)steps.size(); i++){
        ImGui::PushID(i);
        if (ImGui::Selectable(stepLabel(i, steps[i]).c_str(), i == ed.selected)) selectStep(i);
        ImGui::PopID();
    }
    ImGui::Dummy(ImVec2(0, 6));

    // Add a step after the selected one, of the chosen type
    ImGui::SetNextItemWidth(130);
    ImGui::Combo("##type", &ed.addType, STEP_TYPE_LABELS, IM_ARRAYSIZE(STEP_TYPE_LABELS));
    ImGui::SameLine();
    if (ImGui::Button("Add step")){
        LessonStep step;
        step.type = (LessonStepType)ed.addType;
        if (step.type == LessonStepType::Text) step.title = "New step";
        steps.insert(steps.begin() + ed.selected + 1, step);
        selectStep(ed.selected + 1);
        ed.dirty = true;
    }
    ImGui::BeginDisabled(ed.selected == 0);
    if (ImGui::Button("Move up")){
        std::swap(steps[ed.selected], steps[ed.selected - 1]);
        selectStep(ed.selected - 1);
        ed.dirty = true;
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(ed.selected + 1 >= (int)steps.size());
    if (ImGui::Button("Move down")){
        std::swap(steps[ed.selected], steps[ed.selected + 1]);
        selectStep(ed.selected + 1);
        ed.dirty = true;
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(steps.size() <= 1); // a lesson keeps at least one step
    if (ImGui::Button("Delete")){
        steps.erase(steps.begin() + ed.selected);
        selectStep(std::min(ed.selected, (int)steps.size() - 1));
        ed.dirty = true;
    }
    ImGui::EndDisabled();
}

// The file picker: files in the lesson's folder that this step type accepts
static void fileField(LessonStep& step){
    const std::vector<std::string>& extensions = lessonFileExtensions(step.type);
    if (ImGui::BeginCombo("File", step.file.empty() ? "(choose)" : step.file.c_str())){
        for (const std::string& file : ed.folderFiles){
            std::string extension = fs::path(file).extension().string();
            for (char& c : extension) c = (char)std::tolower((unsigned char)c);
            if (std::find(extensions.begin(), extensions.end(), extension) == extensions.end()) continue;
            if (ImGui::Selectable(file.c_str(), file == step.file)){
                step.file = file;
                releaseLessonMedia(ed.media);
                ed.dirty = true;
            }
        }
        ImGui::EndCombo();
    }
    if (ImGui::Button("Open lesson folder")) openFolder(ed.folder);
    ImGui::SameLine();
    if (ImGui::Button("Refresh")) refreshFolderFiles();
    std::string list;
    for (const std::string& extension : extensions) list += (list.empty() ? "" : ", ") + extension;
    ImGui::PushStyleColor(ImGuiCol_Text, ImColor(uiColor(UiColor::Dim)).Value);
    ImGui::TextWrapped("Put %s files in the lesson's folder, then Refresh", list.c_str());
    if (step.type == LessonStepType::Video) ImGui::TextWrapped("%s", VIDEO_CONVERT_HINT);
    ImGui::PopStyleColor();
}

static void fieldsPanel(){
    LessonStep& step = selectedStep();
    ImGui::SeparatorText(TextFormat("Step %d", ed.selected + 1));
    ImGui::PushItemWidth(-80);
    int type = (int)step.type;
    if (ImGui::Combo("Type", &type, STEP_TYPE_LABELS, IM_ARRAYSIZE(STEP_TYPE_LABELS))){
        step.type = (LessonStepType)type; // fields the new type doesn't use are kept, and not saved
        releaseLessonMedia(ed.media);
        ed.dirty = true;
    }
    if (ImGui::InputText("Title", &step.title)) ed.dirty = true;

    switch (step.type){
        case LessonStepType::Text:
            ImGui::TextUnformatted("Text (each line is a paragraph)");
            if (ImGui::InputTextMultiline("##text", &ed.paragraphs, ImVec2(-1, 220), ImGuiInputTextFlags_WordWrap)){
                step.paragraphs.clear();
                std::istringstream lines(ed.paragraphs);
                std::string line;
                while (std::getline(lines, line)) if (!line.empty()) step.paragraphs.push_back(line);
                ed.dirty = true;
            }
            break;
        case LessonStepType::Image:
        case LessonStepType::Audio:
        case LessonStepType::Video:
            fileField(step);
            if (ImGui::InputText("Caption", &step.caption)) ed.dirty = true;
            break;
        case LessonStepType::Exercise: {
            const ExerciseEntry* current = stepExercise(step);
            if (ImGui::BeginCombo("Exercise", current ? current->exercise.title.c_str() : "(choose)")){
                for (const ExerciseEntry& entry : ed.exercises){
                    // A built-in lesson may only use built-in exercises; routines have no goal to check
                    if (!entry.error.empty() || entry.exercise.type == ExerciseType::Routine || (ed.builtIn && !entry.builtIn)) continue;
                    std::string label = entry.exercise.title + (entry.builtIn ? "" : "  (yours)") + "##" + entry.id;
                    if (ImGui::Selectable(label.c_str(), current == &entry)){
                        step.exercise = entry.name;
                        ed.dirty = true;
                    }
                }
                ImGui::EndCombo();
            }
            break;
        }
        case LessonStepType::Play:
            fileField(step);
            break;
    }

    if (step.type == LessonStepType::Exercise || step.type == LessonStepType::Play){
        // Always required to go on: 0 keeps the usual goal
        int goal = step.goal;
        if (ImGui::InputInt("Goal", &goal)){
            step.goal = std::clamp(goal, 0, step.type == LessonStepType::Play ? 100 : 1000);
            ed.dirty = true;
        }
        LessonStep usual = step;
        usual.goal = 0;
        ImGui::TextColored(ImColor(uiColor(UiColor::Dim)), "0 = the usual (%s)", lessonGoalText(usual, stepExercise(step)).c_str());
    }
    ImGui::PopItemWidth();
}

static void previewPanel(){
    ImGui::SeparatorText("Preview");
    ImGui::Dummy(ImVec2(0, 6));
    float width = ImGui::GetContentRegionAvail().x - 20;
    ImGui::Indent(10);
    drawLessonStep(selectedStep(), ed.folder, stepExercise(selectedStep()), ed.media, width);
    ImGui::Unindent(10);
}

// --- Screens --------------------------------------------------------------------------------------------------

static LessonEditorChoice lessonList(){
    LessonEditorChoice choice = LessonEditorChoice::None;
    beginMenu("Lesson editor");
    menuTitle("Lesson editor");
    if (ed.lessons.empty()) centeredText("No lessons yet: make one below");
    for (int i = 0; i < (int)ed.lessons.size(); i++){
        const LessonEntry& entry = ed.lessons[i];
        std::string label = entry.lesson.title + (entry.builtIn ? "  (built-in)" : "  (yours)");
        ImGui::PushID(i);
        if (i == 0) focusNextWhenMenuAppears();
        // A lesson naming a missing exercise opens (it's fixed here); one whose file can't be read stays put, and
        // its error says what to fix
        if (menuButton(label.c_str())) openLesson(entry.folder, entry.builtIn);
        if (!entry.error.empty()) centeredErrorText(entry.error);
        ImGui::PopID();
    }

    ImGui::Dummy(ImVec2(0, 20));
    centeredText("New lesson");
    ImGui::SetCursorPosX((ImGui::GetWindowWidth() - 420) / 2);
    ImGui::SetNextItemWidth(300);
    bool enter = ImGui::InputTextWithHint("##name", "Its name", &ed.newLessonName, ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::SameLine();
    if (ImGui::Button("Create", ImVec2(110, 0)) || enter) createLesson();
    if (!ed.listError.empty()) centeredErrorText(ed.listError);

    ImGui::Dummy(ImVec2(0, 20));
    if (menuButton("Open lessons folder")){
        std::error_code ec;
        fs::create_directories(ed.setup.userLessons, ec);
        openFolder(ed.setup.userLessons);
    }
    if (menuButton("Back")) choice = LessonEditorChoice::Back;
    if (ImGui::IsKeyPressed(ImGuiKey_Escape) && !ImGui::GetIO().WantTextInput) choice = LessonEditorChoice::Back;
    ImGui::End();
    return choice;
}

static void leaveLesson(){
    releaseLessonMedia(ed.media);
    ed.editing = false;
    refreshLists(); // a new copy, a new title or a fixed error shows in the list
}

static void lessonEditing(){
    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGui::Begin("Lesson", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings
                                   | ImGuiWindowFlags_NoBackground);

    bool requestBack = false;
    if (ImGui::Button("Save")) saveEditedLesson();
    ImGui::SameLine();
    if (ImGui::Button("Back")) requestBack = true;
    ImGui::SameLine();
    ImGui::Text("%s%s", ed.lesson.title.c_str(), ed.dirty ? "  *" : "");
    ImGui::SameLine();
    ImGui::TextDisabled("%s", ed.status.c_str());
    ImGui::Separator();

    ImGui::BeginChild("Steps", ImVec2(STEPS_PANEL_WIDTH, 0));
    stepsPanel();
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("Fields", ImVec2(FIELDS_PANEL_WIDTH, 0));
    fieldsPanel();
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("Preview", ImVec2(0, 0), ImGuiChildFlags_Borders);
    previewPanel();
    ImGui::EndChild();

    if (!ImGui::IsPopupOpen(UNSAVED_POPUP)){
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S)) saveEditedLesson();
        if (ImGui::IsKeyPressed(ImGuiKey_Escape) && !io.WantTextInput) requestBack = true;
    }
    if (requestBack){
        if (ed.dirty) ImGui::OpenPopup(UNSAVED_POPUP);
        else leaveLesson();
    }

    // Leaving with unsaved changes asks first, as in the chart editor
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal(UNSAVED_POPUP, nullptr, ImGuiWindowFlags_AlwaysAutoResize)){
        ImGui::Text("Save your changes before leaving?");
        if (ImGui::Button("Save")){
            if (saveEditedLesson()) leaveLesson();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Discard")){
            leaveLesson();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    ImGui::End();
}

void openLessonEditor(const LessonEditorSetup& setup){
    closeLessonEditor();
    ed.setup = setup;
    ed.editing = false;
    ed.listError.clear();
    ed.active = true;
    refreshLists();
}

LessonEditorChoice lessonEditorScreen(){
    if (ed.editing){
        lessonEditing();
        return LessonEditorChoice::None;
    }
    return lessonList();
}

void closeLessonEditor(){
    if (!ed.active) return;
    releaseLessonMedia(ed.media);
    ed.active = false;
}
