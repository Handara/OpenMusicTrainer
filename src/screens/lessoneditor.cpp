#include "screens/lessoneditor.h"

#include "core/chart.h"
#include "core/files.h"
#include "core/lessondoc.h"
#include "core/songlibrary.h"
#include "imgui.h"
#include "imgui_stdlib.h"
#include "learn/exercise.h"
#include "learn/lessonpage.h"
#include "raylib.h"
#include "screens/learnscreen.h"
#include "ui/menulist.h"
#include "ui/theme.h"
#include "ui/ui.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <filesystem>
#include <map>
#include <memory>
#include <sstream>

namespace fs = std::filesystem;

// At a 720-pixel-tall window; everything scales with it
const float TOP_BAR = 64.0f;
const float OUTLINE_WIDTH = 250.0f;
const float INSPECTOR_WIDTH = 320.0f;
const float PANEL_GAP = 16.0f;
const float CANVAS_PAD = 26.0f;
const float SCROLL_STEP = 60.0f;
const float DRAG_START = 6.0f;      // the mouse moved this far with the button down: it's dragging, not clicking
const float EDGE_SCROLL = 600.0f;   // dragging near the page's top or bottom scrolls it, this fast (pixels a second)
const int MOST_UNDOS = 100;
const char* const UNSAVED_POPUP = "Unsaved changes";

namespace {

// What's chosen to change: the lesson's details, a page, a section of it, or a block in it
enum class Picked { Lesson, Page, Section, Block };

// Something being dragged onto the page: a block already on it (to move), or a new one (from the blocks to add)
struct Drag {
    enum Kind { None, Pending, Moving, Adding } kind = None; // Pending: the button's down on a block, not moved far yet
    BlockPlace from;
    BlockType type = BlockType::Text;
    ImVec2 start;
};

// The blocks offered to add, in the order they're offered
struct PaletteGroup {
    const char* title;
    std::vector<BlockType> types;
};
const std::vector<PaletteGroup>& palette(){
    static const std::vector<PaletteGroup> groups = {
        { "WORDS", { BlockType::Text, BlockType::Heading, BlockType::Callout, BlockType::Reveal } },
        { "DIAGRAMS", { BlockType::Fretboard, BlockType::Keyboard, BlockType::Staff } },
        { "MEDIA", { BlockType::Image, BlockType::Audio, BlockType::Video } },
        { "TO PLAY", { BlockType::Exercise, BlockType::Play } },
        { "SMART", { BlockType::Practice } },
    };
    return groups;
}

} // namespace

static struct {
    LessonEditorSetup setup;
    std::vector<LessonEntry> lessons;      // the list to pick from
    std::vector<ExerciseEntry> exercises;  // for exercise blocks to name
    std::vector<SongEntry> songs;          // the game's songs, for song blocks to play
    std::string newLessonName;
    int newTemplate = 0;                   // the new lesson's template (lessonTemplates), and its instrument
    int newInstrument = 1;                 //   (as ExerciseInstrument: guitar)
    bool focusName = false;                // the keyboard in the new lesson's name field, next frame
    std::string listError;

    // The lesson being made (editing = true)
    bool editing = false;
    LessonDoc doc;
    std::string folder;
    bool builtIn = false;
    bool dirty = false;
    std::string status;                    // the last save's result
    std::string problem;                   // what stops it being played yet ("" for nothing)
    std::vector<LessonDoc> undos, redos;
    std::vector<std::string> folderFiles;  // the lesson folder's files, for blocks to pick from

    int page = 0;                          // the page shown
    Picked picked = Picked::Page;
    int section = 0;                       // the section chosen (Picked::Section), or the chosen block's
    BlockPlace block;                      // the block chosen (Picked::Block)
    std::map<std::string, std::string> fields; // the inspector's fields as typed: kept until something else is chosen
    std::string fieldErrors;               // what's wrong with what's typed, if anything
    float scroll = 0.0f, pageHeight = 0.0f;
    PageMedia media;
    Drag drag;

    std::unique_ptr<Exercise> tryout;      // the lesson played from a page, while it's tried out
    bool active = false;
} ed;

// --- Lists ----------------------------------------------------------------------------------------------------------

static void refreshLists(){
    ed.exercises = scanExercises(ed.setup.builtInExercises, true);
    std::vector<ExerciseEntry> user = scanExercises(ed.setup.userExercises, false);
    ed.exercises.insert(ed.exercises.end(), user.begin(), user.end());
    checkRoutines(ed.exercises);
    ed.lessons = scanLessons(ed.setup.builtInLessons, true);
    std::vector<LessonEntry> userLessons = scanLessons(ed.setup.userLessons, false);
    ed.lessons.insert(ed.lessons.end(), userLessons.begin(), userLessons.end());
    checkLessonExercises(ed.lessons, ed.exercises);
    checkLessonSongs(ed.lessons, ed.setup.songFolders);
    ed.songs.clear();
    for (size_t i = 0; i < ed.setup.songFolders.size(); i++){
        std::vector<SongEntry> found = scanSongs(ed.setup.songFolders[i], i == 0);
        for (const SongEntry& song : found) if (song.error.empty()) ed.songs.push_back(song);
    }
}

// A song of the game's by its folder's name (a song block's), or nullptr
static const SongEntry* findSong(const std::string& name){
    for (const SongEntry& song : ed.songs) if (fs::path(song.folder).filename().string() == name) return &song;
    return nullptr;
}

static void refreshFolderFiles(){
    ed.folderFiles.clear();
    std::error_code ec;
    for (const fs::directory_entry& file : fs::directory_iterator(ed.folder, ec))
        if (file.is_regular_file() && file.path().filename() != LESSON_FILE_NAME) ed.folderFiles.push_back(file.path().filename().string());
    std::sort(ed.folderFiles.begin(), ed.folderFiles.end());
}

// --- Changes ----------------------------------------------------------------------------------------------------------

// A kind of exercise's starter, for this lesson: on a piano, played on the keys
static std::string exerciseStarter(const ExerciseForm& form){
    std::string lines = form.starter;
    const std::string type = form.type;
    if (ed.doc.instrument == ExerciseInstrument::Piano && (type == "notes" || type == "reading")){
        lines += "\ninstrument piano";
        if (type == "notes") lines.replace(lines.find("show staff"), 10, "show keys");
    }
    return lines;
}


// What still stops the lesson being played, said for its maker: the first thing found
static std::string findProblem(){
    for (const BlockPlace& place : lessonBlocks(ed.doc)){
        const LessonBlock& block = blockAt(ed.doc, place);
        const BlockInfo& info = blockInfo(block.type);
        const std::string where = "Page " + std::to_string(place.page + 1) + ": ";
        for (const BlockField& field : info.fields)
            if (field.required && blockValues(block, field.key).empty())
                return where + "a " + std::string(info.name) + " block still needs its " + std::string(field.name);
        for (const char* key : { "help", "ace" }){
            const std::string title = blockValue(block, key);
            if (!title.empty() && findPage(ed.doc, title) < 0) return where + "there's no page called '" + title + "' to send the student to";
        }
        if (block.type == BlockType::Play){
            const std::string song = blockValue(block, "song");
            if (song.empty() && blockValue(block, "file").empty()) return where + "a song block still needs its song";
            if (!song.empty() && !findSong(song)) return where + "there's no song called " + song;
        }
        if (block.type == BlockType::Exercise){
            const std::string name = blockValue(block, "exercise");
            if (name.empty() && block.exerciseLines.empty()) return where + "an exercise block still needs its exercise";
            if (!name.empty() && !findExercise(ed.exercises, ed.builtIn, name)) return where + "there's no exercise called " + name;
        }
    }
    std::string error;
    if (!checkLessonFiles(ed.doc, ed.folder, error)) return error;
    if (ed.doc.title.empty()) return "The lesson needs a title";
    return "";
}

// The page, section and block chosen kept to what the lesson has (after an undo, a page or block taken out)
static void keepChoiceInside(){
    ed.page = std::clamp(ed.page, 0, std::max(0, (int)ed.doc.pages.size() - 1));
    if (ed.doc.pages.empty()){
        ed.picked = Picked::Lesson;
        return;
    }
    const LessonPage& page = ed.doc.pages[(size_t)ed.page];
    if (ed.picked == Picked::Section && ed.section >= (int)page.sections.size()) ed.picked = Picked::Page;
    if (ed.picked == Picked::Block){
        ed.block.page = ed.page;
        if (!blockPointer(ed.doc, ed.block)) ed.picked = page.sections.empty() ? Picked::Page : Picked::Section;
        ed.section = std::clamp(ed.section, 0, std::max(0, (int)page.sections.size() - 1));
    }
}

static void choose(Picked picked, int section = 0, BlockPlace block = {}){
    ed.picked = picked;
    ed.section = section;
    ed.block = block;
    ed.block.page = ed.page;
    ed.fields.clear(); // the inspector's typing starts from what's chosen now
    ed.fieldErrors.clear();
}

// Before each change: the lesson as it was, to go back to
static void beforeChange(){
    ed.undos.push_back(ed.doc);
    if ((int)ed.undos.size() > MOST_UNDOS) ed.undos.erase(ed.undos.begin());
    ed.redos.clear();
}

// After each change: unsaved, and what stops it being played looked at again
static void changed(){
    ed.dirty = true;
    ed.status.clear();
    ed.problem = findProblem();
}

static void undo(){
    if (ed.undos.empty()) return;
    ed.redos.push_back(ed.doc);
    ed.doc = ed.undos.back();
    ed.undos.pop_back();
    keepChoiceInside();
    ed.fields.clear();
    changed();
}

static void redo(){
    if (ed.redos.empty()) return;
    ed.undos.push_back(ed.doc);
    ed.doc = ed.redos.back();
    ed.redos.pop_back();
    keepChoiceInside();
    ed.fields.clear();
    changed();
}

static void showPage(int page){
    if (page == ed.page && !ed.doc.pages.empty()) return;
    releasePageMedia(ed.media);
    ed.page = std::clamp(page, 0, std::max(0, (int)ed.doc.pages.size() - 1));
    ed.scroll = 0.0f;
    choose(Picked::Page);
}

// Where a new block goes: after the one chosen, else at the end of the chosen section's first column, else of the
// page's last section (a page with none gets one)
static BlockPlace placeForNew(){
    LessonPage& page = ed.doc.pages[(size_t)ed.page];
    if (ed.picked == Picked::Block && blockPointer(ed.doc, ed.block)) return { ed.page, ed.block.section, ed.block.column, ed.block.block + 1 };
    if (page.sections.empty()) page.sections.push_back(makeSection(SectionLayout::Single));
    const int section = ed.picked == Picked::Section ? std::min(ed.section, (int)page.sections.size() - 1) : (int)page.sections.size() - 1;
    return { ed.page, section, 0, (int)page.sections[(size_t)section].columns[0].size() };
}

static void addBlock(BlockType type, BlockPlace at){
    beforeChange();
    LessonBlock block = makeBlock(type);
    if (type == BlockType::Exercise){ // something to start from: a few notes to play
        std::string error;
        setBlockExercise(block, exerciseStarter(exerciseForms().front()), error);
    }
    if (ed.doc.pages[(size_t)ed.page].sections.empty()) ed.doc.pages[(size_t)ed.page].sections.push_back(makeSection(SectionLayout::Single));
    if (!insertBlock(ed.doc, at, block)){
        ed.undos.pop_back();
        return;
    }
    choose(Picked::Block, at.section, at);
    changed();
}

static void addSection(SectionLayout layout){
    beforeChange();
    LessonPage& page = ed.doc.pages[(size_t)ed.page];
    // After the chosen section (or the chosen block's), else at the end
    const int after = ed.picked == Picked::Section ? ed.section : ed.picked == Picked::Block ? ed.block.section : (int)page.sections.size() - 1;
    const int at = std::clamp(after + 1, 0, (int)page.sections.size());
    page.sections.insert(page.sections.begin() + at, makeSection(layout));
    choose(Picked::Section, at);
    changed();
}

// A page from a template (core/lessondoc pageTemplates), after the one shown
static void addPage(const std::string& templateId){
    beforeChange();
    const int at = ed.doc.pages.empty() ? 0 : ed.page + 1;
    LessonPage page = makePage(templateId, ed.doc.instrument);
    // Its title its own (a page a block sends the student to is found by it)
    const std::string title = page.title;
    for (int n = 2; findPage(ed.doc, page.title) >= 0; n++) page.title = title + " " + std::to_string(n);
    ed.doc.pages.insert(ed.doc.pages.begin() + at, page);
    releasePageMedia(ed.media);
    ed.page = at;
    ed.scroll = 0.0f;
    choose(Picked::Page);
    changed();
}

static void removeChosen(){
    if (ed.doc.pages.empty()) return;
    LessonPage& page = ed.doc.pages[(size_t)ed.page];
    if (ed.picked == Picked::Block){
        beforeChange();
        removeBlock(ed.doc, ed.block);
        choose(Picked::Section, ed.block.section);
    } else if (ed.picked == Picked::Section && ed.section < (int)page.sections.size()){
        beforeChange();
        page.sections.erase(page.sections.begin() + ed.section);
        choose(Picked::Page);
    } else if (ed.picked == Picked::Page && ed.doc.pages.size() > 1){ // a lesson keeps one page at least
        beforeChange();
        ed.doc.pages.erase(ed.doc.pages.begin() + ed.page);
        releasePageMedia(ed.media);
        ed.page = std::min(ed.page, (int)ed.doc.pages.size() - 1);
        choose(Picked::Page);
    } else return;
    changed();
}

static void duplicateChosen(){
    if (ed.picked == Picked::Block){
        const LessonBlock* block = blockPointer(ed.doc, ed.block);
        if (!block) return;
        const LessonBlock copy = *block;
        beforeChange();
        BlockPlace at = ed.block;
        at.block++;
        insertBlock(ed.doc, at, copy);
        choose(Picked::Block, at.section, at);
    } else if (ed.picked == Picked::Page){
        beforeChange();
        LessonPage copy = ed.doc.pages[(size_t)ed.page];
        ed.doc.pages.insert(ed.doc.pages.begin() + ed.page + 1, copy);
        releasePageMedia(ed.media);
        ed.page++;
        choose(Picked::Page);
    } else return;
    changed();
}

// The chosen block one place up or down its column (out of its column: into the section before or after it); the
// chosen section or page one place earlier or later
static void moveChosen(int by){
    if (ed.doc.pages.empty()) return;
    LessonPage& page = ed.doc.pages[(size_t)ed.page];
    if (ed.picked == Picked::Block){
        const std::vector<LessonBlock>& column = page.sections[(size_t)ed.block.section].columns[(size_t)ed.block.column];
        BlockPlace to = ed.block;
        if (by < 0 && ed.block.block > 0) to.block = ed.block.block - 1;
        else if (by > 0 && ed.block.block + 1 < (int)column.size()) to.block = ed.block.block + 2;
        else if (by < 0 && ed.block.section > 0){ // to the end of the section above, its first column
            to.section = ed.block.section - 1;
            to.column = 0;
            to.block = (int)page.sections[(size_t)to.section].columns[0].size();
        } else if (by > 0 && ed.block.section + 1 < (int)page.sections.size()){ // to the start of the one below
            to.section = ed.block.section + 1;
            to.column = 0;
            to.block = 0;
        } else return;
        beforeChange();
        const BlockPlace landed = moveBlock(ed.doc, ed.block, to);
        choose(Picked::Block, landed.section, landed);
    } else if (ed.picked == Picked::Section){
        const int to = ed.section + by;
        if (to < 0 || to >= (int)page.sections.size()) return;
        beforeChange();
        std::swap(page.sections[(size_t)ed.section], page.sections[(size_t)to]);
        choose(Picked::Section, to);
    } else if (ed.picked == Picked::Page){
        const int to = ed.page + by;
        if (to < 0 || to >= (int)ed.doc.pages.size()) return;
        beforeChange();
        std::swap(ed.doc.pages[(size_t)ed.page], ed.doc.pages[(size_t)to]);
        ed.page = to;
        choose(Picked::Page);
    } else return;
    changed();
}

// --- Opening and saving -----------------------------------------------------------------------------------------------

static void openLesson(const std::string& folder, bool builtIn){
    std::string error;
    LessonDoc doc;
    if (!loadLessonDraft(folder, doc, error)){
        ed.listError = error;
        return;
    }
    ed.doc = doc;
    ed.folder = folder;
    ed.builtIn = builtIn;
    ed.dirty = false;
    ed.undos.clear();
    ed.redos.clear();
    ed.status = builtIn ? "Built in: saving makes your own copy" : "";
    ed.editing = true;
    ed.page = 0;
    ed.scroll = 0.0f;
    refreshFolderFiles();
    choose(Picked::Page);
    ed.problem = findProblem();
}

static void createLesson(){
    const std::string folderName = safeFolderName(ed.newLessonName);
    if (folderName.empty()){
        ed.listError = "Give the lesson a name (letters, digits, spaces, - and _)";
        return;
    }
    const fs::path folder = fs::path(ed.setup.userLessons) / folderName;
    if (fs::exists(folder)){
        ed.listError = "There's already a lesson folder called '" + folderName + "'";
        return;
    }
    // Something to start from: the template chosen, for the instrument chosen
    const LessonDoc doc = makeLesson(lessonTemplates()[(size_t)ed.newTemplate].id, ed.newLessonName, (ExerciseInstrument)ed.newInstrument);
    std::string error;
    if (!saveLessonDoc(folder.string(), doc, error)){
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
        const fs::path source = ed.folder;
        fs::path destination = fs::path(ed.setup.userLessons) / source.filename();
        for (int n = 2; fs::exists(destination); n++) destination = fs::path(ed.setup.userLessons) / (source.filename().string() + " (" + std::to_string(n) + ")");
        std::error_code ec;
        fs::create_directories(destination.parent_path(), ec);
        if (!ec) fs::copy(source, destination, fs::copy_options::recursive, ec);
        if (ec){
            ed.status = "Couldn't copy the lesson: " + ec.message();
            return false;
        }
        ed.folder = destination.string();
        ed.builtIn = false;
    }
    std::string error;
    if (!saveLessonDoc(ed.folder, ed.doc, error)){
        ed.status = error;
        return false;
    }
    ed.dirty = false;
    ed.status = "Saved";
    ed.problem = findProblem();
    return true;
}

// --- Drawing helpers ----------------------------------------------------------------------------------------------------

// A pill-shaped button drawn on the window; true when clicked
static bool pill(ImVec2 at, const char* label, bool lit, bool live, float s, float* width = nullptr){
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const UiFonts& fonts = uiFonts();
    const ImVec2 extent = fonts.bold->CalcTextSizeA(15 * s, FLT_MAX, 0.0f, label);
    const ImVec2 b(at.x + extent.x + 28 * s, at.y + 32 * s);
    if (width) *width = b.x - at.x;
    const bool hovered = live && ImGui::IsMouseHoveringRect(at, b);
    draw->AddRectFilled(at, b, lit && live ? uiColor(UiColor::Accent) : hovered ? uiColor(UiColor::Accent, 0.15f) : uiColor(UiColor::Card), 16 * s);
    draw->AddRect(at, b, uiColor(hovered || (lit && live) ? UiColor::Accent : UiColor::StaffLine, live ? 1.0f : 0.5f), 16 * s, 0, 1.0f);
    draw->AddText(fonts.bold, 15 * s, ImVec2(at.x + 14 * s, at.y + (32 * s - extent.y) / 2),
                  uiColor(lit && live ? UiColor::Background : live ? UiColor::Ink : UiColor::Dim), label);
    return hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left);
}

// A small label in capitals over a group of things in a panel
static void panelHeading(const char* text){
    ImGui::Dummy(ImVec2(0, 6 * menuScale()));
    ImGui::PushStyleColor(ImGuiCol_Text, uiColorVec(UiColor::Accent));
    ImGui::PushFont(uiFonts().mono, 12 * menuScale());
    ImGui::TextUnformatted(text);
    ImGui::PopFont();
    ImGui::PopStyleColor();
}

static void dimText(const char* text){
    ImGui::PushStyleColor(ImGuiCol_Text, uiColorVec(UiColor::Dim));
    ImGui::PushFont(uiFonts().text, 13 * menuScale());
    ImGui::TextWrapped("%s", text);
    ImGui::PopFont();
    ImGui::PopStyleColor();
}

// A text field whose typing is one step to undo (taken when the typing starts)
static bool undoableInput(const char* id, std::string* value, bool multiline = false, float height = 0.0f){
    ImGui::SetNextItemWidth(-1);
    const bool edited = multiline ? ImGui::InputTextMultiline(id, value, ImVec2(-1, height)) : ImGui::InputText(id, value);
    if (ImGui::IsItemActivated()) beforeChange();
    return edited;
}

// --- The outline: pages, and what to add --------------------------------------------------------------------------------

static void outlinePanel(float s){
    panelHeading("PAGES");
    for (int i = 0; i < (int)ed.doc.pages.size(); i++){
        const std::string& title = ed.doc.pages[(size_t)i].title;
        const std::string label = std::to_string(i + 1) + "   " + (title.empty() ? "(no title)" : title)
                                + (ed.doc.pages[(size_t)i].aside ? "   (help)" : "") + "##page" + std::to_string(i);
        if (ImGui::Selectable(label.c_str(), i == ed.page)){
            showPage(i);
            choose(Picked::Page);
        }
    }
    if (ImGui::Button("+ Page", ImVec2(-1, 0))) ImGui::OpenPopup("page templates");
    if (ImGui::BeginPopup("page templates")){
        for (const LessonTemplate& kind : pageTemplates()){
            if (ImGui::Selectable(kind.name)) addPage(kind.id);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", kind.description);
        }
        ImGui::EndPopup();
    }

    panelHeading("ADD A BLOCK");
    dimText("Added after the block chosen. Or drag it onto the page.");
    const float half = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) / 2;
    for (const PaletteGroup& group : palette()){
        ImGui::PushStyleColor(ImGuiCol_Text, uiColorVec(UiColor::Dim));
        ImGui::PushFont(uiFonts().mono, 11 * s);
        ImGui::TextUnformatted(group.title);
        ImGui::PopFont();
        ImGui::PopStyleColor();
        for (size_t i = 0; i < group.types.size(); i++){
            if (i % 2 == 1) ImGui::SameLine();
            const BlockInfo& info = blockInfo(group.types[i]);
            if (ImGui::Button(info.name, ImVec2(half, 0)) && ed.drag.kind != Drag::Adding) addBlock(info.type, placeForNew());
            if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left, DRAG_START * s) && ed.drag.kind == Drag::None){
                ed.drag.kind = Drag::Adding; // dragged out: dropped where it's let go
                ed.drag.type = info.type;
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", info.description);
        }
    }
    panelHeading("ADD A SECTION");
    dimText("Blocks side by side, in columns.");
    for (int layout = 0; layout < (int)SectionLayout::Count; layout++){
        if (layout % 2 == 1) ImGui::SameLine();
        if (ImGui::Button(sectionLayoutName((SectionLayout)layout), ImVec2(half, 0))) addSection((SectionLayout)layout);
    }
}

// --- The inspector: what's chosen, and its settings ------------------------------------------------------------------

// An exercise written in a block, as a form: its kind, then its settings, each read by the exercise's own reader as
// it's typed (kept once it reads); everything else it says, as text, folded away
static void exerciseForm(LessonBlock& block){
    const float s = menuScale();
    const std::string type = exerciseSetting(block, "type");
    const ExerciseForm* form = findExerciseForm(type);
    ImGui::PushFont(uiFonts().bold, 15 * s);
    ImGui::TextUnformatted("Kind of exercise");
    ImGui::PopFont();
    ImGui::SetNextItemWidth(-1);
    if (ImGui::BeginCombo("##exercisekind", form ? form->name : type.c_str())){
        for (const ExerciseForm& each : exerciseForms()){
            if (!ImGui::Selectable(each.name, form == &each) || form == &each) continue;
            beforeChange();
            std::string error;
            if (setBlockExercise(block, exerciseStarter(each), error)){
                ed.fields.clear();
                changed();
            } else ed.undos.pop_back();
        }
        ImGui::EndCombo();
    }
    if (form) dimText(form->description);
    for (const BlockField& field : form ? form->fields : std::vector<BlockField>{}){
        ImGui::Dummy(ImVec2(0, 3 * s));
        ImGui::PushFont(uiFonts().bold, 15 * s);
        ImGui::TextUnformatted(field.name);
        ImGui::PopFont();
        const std::string id = std::string("##x") + field.key;
        const std::string current = exerciseSetting(block, field.key);
        auto apply = [&](const std::string& value){
            std::string error;
            if (setExerciseSetting(block, field.key, value, error)){
                ed.fieldErrors.clear();
                ed.fields.erase("lines"); // the text below shows the change
                changed();
                return true;
            }
            ed.fieldErrors = error;
            return false;
        };
        if (field.kind == FieldKind::Toggle){
            bool on = (current.empty() ? std::string(field.standard) : current) == "yes";
            if (ImGui::Checkbox((std::string(field.description) + id).c_str(), &on)){
                beforeChange();
                if (!apply(on ? "yes" : "no")) ed.undos.pop_back();
            }
            continue;
        }
        if (field.kind == FieldKind::Choice){
            const std::string shown = current.empty() ? std::string(field.standard) : current;
            ImGui::SetNextItemWidth(-1);
            if (ImGui::BeginCombo(id.c_str(), shown.c_str())){
                for (const std::string& choice : field.choices){
                    if (!ImGui::Selectable(choice.c_str(), choice == shown) || choice == shown) continue;
                    beforeChange();
                    if (!apply(choice)) ed.undos.pop_back();
                }
                ImGui::EndCombo();
            }
        } else {
            const std::string key = std::string("x:") + field.key;
            if (!ed.fields.count(key)) ed.fields[key] = current;
            ImGui::SetNextItemWidth(-1);
            if (ImGui::InputTextWithHint(id.c_str(), field.standard, &ed.fields[key])) apply(ed.fields[key]);
            if (ImGui::IsItemActivated()) beforeChange();
        }
        dimText(field.description);
    }
    // Everything it says, as in an .exercise file: for what the form doesn't show
    ImGui::Dummy(ImVec2(0, 4 * s));
    if (ImGui::CollapsingHeader("All its settings, as text")){
        if (!ed.fields.count("lines")){
            std::string joined;
            for (const std::string& line : block.exerciseLines) joined += line + "\n";
            ed.fields["lines"] = joined;
        }
        if (undoableInput("##lines", &ed.fields["lines"], true, 130 * s)){
            std::string error;
            if (setBlockExercise(block, ed.fields["lines"], error)){
                ed.fieldErrors.clear();
                for (auto it = ed.fields.begin(); it != ed.fields.end();) it = it->first.rfind("x:", 0) == 0 ? ed.fields.erase(it) : std::next(it);
                changed();
            } else ed.fieldErrors = error;
        }
        dimText("As in an .exercise file: a setting a line.");
    }
}

// One setting of the chosen block, as its kind wants it
static void blockField(LessonBlock& block, const BlockField& field){
    const float s = menuScale();
    ImGui::PushFont(uiFonts().bold, 15 * s);
    ImGui::TextUnformatted(field.name);
    ImGui::PopFont();
    const std::string id = std::string("##") + field.key;
    switch (field.kind){
        case FieldKind::Paragraphs: {
            if (!ed.fields.count(field.key)){
                std::string joined;
                for (const std::string& paragraph : blockValues(block, field.key)) joined += (joined.empty() ? "" : "\n") + paragraph;
                ed.fields[field.key] = joined;
            }
            if (undoableInput(id.c_str(), &ed.fields[field.key], true, 110 * s)){
                std::vector<std::string> paragraphs;
                std::istringstream lines(ed.fields[field.key]);
                std::string line;
                while (std::getline(lines, line)) if (!line.empty()) paragraphs.push_back(line);
                setBlockValues(block, field.key, paragraphs);
                changed();
            }
            dimText("A line each paragraph. **Bold** words; [E4] a note to click and hear.");
            return; // (that says it all)
        }
        case FieldKind::Toggle: {
            bool on = blockValue(block, field.key) == "yes";
            if (ImGui::Checkbox((std::string(field.description) + id).c_str(), &on)){
                beforeChange();
                setBlockValue(block, field.key, on ? "yes" : "no");
                changed();
            }
            return; // its description is its label
        }
        case FieldKind::Choice: {
            const std::string current = blockValue(block, field.key);
            ImGui::SetNextItemWidth(-1);
            if (ImGui::BeginCombo(id.c_str(), current.c_str())){
                for (const std::string& choice : field.choices){
                    if (ImGui::Selectable(choice.c_str(), choice == current)){
                        beforeChange();
                        setBlockValue(block, field.key, choice);
                        changed();
                    }
                }
                ImGui::EndCombo();
            }
            break;
        }
        case FieldKind::File: {
            const std::string current = blockValue(block, field.key);
            ImGui::SetNextItemWidth(-1);
            if (ImGui::BeginCombo(id.c_str(), current.empty() ? "Choose a file" : current.c_str())){
                for (const std::string& file : ed.folderFiles){
                    std::string extension = fs::path(file).extension().string();
                    for (char& c : extension) c = (char)std::tolower((unsigned char)c);
                    if (std::find(field.choices.begin(), field.choices.end(), extension) == field.choices.end()) continue;
                    if (ImGui::Selectable(file.c_str(), file == current)){
                        beforeChange();
                        setBlockValue(block, field.key, file);
                        if (block.type == BlockType::Play) setBlockValue(block, "song", ""); // a file instead of a song
                        releasePageMedia(ed.media);
                        changed();
                    }
                }
                ImGui::EndCombo();
            }
            if (ImGui::IsItemClicked()) refreshFolderFiles(); // files put there meanwhile
            if (ImGui::Button("Open the lesson's folder")){
                if (ed.builtIn) ed.status = "Built in: save first, to have your own copy and its folder";
                else openFolder(ed.folder);
            }
            break;
        }
        case FieldKind::Page: {
            // The lesson's pages by their titles (the ones set aside for help first): where the block sends the student
            const std::string current = blockValue(block, field.key);
            ImGui::SetNextItemWidth(-1);
            if (ImGui::BeginCombo(id.c_str(), current.empty() ? "Nowhere" : current.c_str())){
                if (ImGui::Selectable("Nowhere", current.empty()) && !current.empty()){
                    beforeChange();
                    setBlockValue(block, field.key, "");
                    changed();
                }
                for (int pass = 0; pass < 2; pass++){
                    for (const LessonPage& page : ed.doc.pages){
                        if (page.aside != (pass == 0) || page.title.empty()) continue;
                        const std::string label = page.title + (page.aside ? "   (help)" : "");
                        if (!ImGui::Selectable(label.c_str(), page.title == current) || page.title == current) continue;
                        beforeChange();
                        setBlockValue(block, field.key, page.title);
                        changed();
                    }
                }
                ImGui::EndCombo();
            }
            break;
        }
        case FieldKind::Song: {
            // The game's songs, by their titles: one chosen plays instead of a file
            const std::string current = blockValue(block, field.key);
            const SongEntry* chosen = current.empty() ? nullptr : findSong(current);
            ImGui::SetNextItemWidth(-1);
            if (ImGui::BeginCombo(id.c_str(), chosen ? chosen->title.c_str() : current.empty() ? "Choose a song" : (current + " (not found)").c_str(),
                                  ImGuiComboFlags_HeightLarge)){
                for (const SongEntry& song : ed.songs){
                    const std::string name = fs::path(song.folder).filename().string();
                    const std::string label = song.title + (song.artist.empty() ? "" : "  ·  " + song.artist) + "##" + song.folder;
                    if (!ImGui::Selectable(label.c_str(), name == current) || name == current) continue;
                    beforeChange();
                    setBlockValue(block, field.key, name);
                    setBlockValue(block, "file", "");
                    setBlockValue(block, "part", "");
                    if (block.name.empty() || (chosen && block.name == chosen->title)) block.name = song.title; // its card says it
                    changed();
                }
                ImGui::EndCombo();
            }
            break;
        }
        case FieldKind::Exercise: {
            // One of the game's, by its title; or one written here
            const std::string current = blockValue(block, field.key);
            const ExerciseEntry* named = current.empty() ? nullptr : findExercise(ed.exercises, ed.builtIn, current);
            const std::string shown = !current.empty() ? (named ? named->exercise.title : current + " (not found)")
                                    : block.exerciseLines.empty() ? "Choose an exercise" : "Written here, below";
            ImGui::SetNextItemWidth(-1);
            if (ImGui::BeginCombo(id.c_str(), shown.c_str(), ImGuiComboFlags_HeightLarge)){
                if (ImGui::Selectable("Written here, below", current.empty() && !block.exerciseLines.empty())){
                    beforeChange();
                    std::string error;
                    if (block.exerciseLines.empty()) setBlockExercise(block, exerciseStarter(exerciseForms().front()), error);
                    else setBlockValue(block, "exercise", "");
                    ed.fields.erase("lines");
                    changed();
                }
                for (const ExerciseEntry& entry : ed.exercises){
                    if (!entry.error.empty() || entry.exercise.type == ExerciseType::Routine || (ed.builtIn && !entry.builtIn)) continue;
                    if (ImGui::Selectable((entry.exercise.title + "##" + entry.id).c_str(), entry.name == current)){
                        beforeChange();
                        setBlockValue(block, field.key, entry.name);
                        block.exerciseLines.clear();
                        block.exercise = ExerciseFile{};
                        changed();
                    }
                }
                ImGui::EndCombo();
            }
            dimText(field.description);
            if (current.empty() && !block.exerciseLines.empty()) exerciseForm(block);
            return;
        }
        case FieldKind::Number:
            if (block.type == BlockType::Play && field.key == std::string("part")){
                // A song's parts, by their names
                if (const SongEntry* song = findSong(blockValue(block, "song")); song && !song->parts.empty()){
                    const int part = std::clamp(std::atoi(blockValue(block, "part").c_str()), 1, (int)song->parts.size());
                    auto partName = [&](int n){
                        const SongPart& each = song->parts[(size_t)n - 1];
                        return std::to_string(n) + "  " + each.name + (each.type == InstrumentType::Bass ? " (bass)" : each.type == InstrumentType::Keys ? " (keys)" : " (guitar)");
                    };
                    ImGui::SetNextItemWidth(-1);
                    if (ImGui::BeginCombo(id.c_str(), partName(part).c_str())){
                        for (int n = 1; n <= (int)song->parts.size(); n++){
                            if (!ImGui::Selectable(partName(n).c_str(), n == part) || n == part) continue;
                            beforeChange();
                            setBlockValue(block, field.key, n == 1 ? "" : std::to_string(n));
                            changed();
                        }
                        ImGui::EndCombo();
                    }
                    break;
                }
            }
            [[fallthrough]];
        default: { // a line of text, a number, notes, places on the neck...: kept once it reads right
            if (!ed.fields.count(field.key)) ed.fields[field.key] = blockValues(block, field.key).empty() ? "" : blockValue(block, field.key);
            if (undoableInput(id.c_str(), &ed.fields[field.key])){
                const std::string& typed = ed.fields[field.key];
                const std::string problem = typed.empty() ? (field.required ? std::string("'") + field.key + "' can't be left empty" : "")
                                                          : checkBlockValue(field, typed);
                if (problem.empty()){
                    setBlockValue(block, field.key, typed);
                    if (field.key == std::string("frets") || field.kind == FieldKind::Text) releasePageMedia(ed.media);
                    ed.fieldErrors.clear();
                    changed();
                } else ed.fieldErrors = problem;
            }
            break;
        }
    }
    dimText(field.description);
}

static void inspectorPanel(float s){
    LessonPage* page = ed.doc.pages.empty() ? nullptr : &ed.doc.pages[(size_t)ed.page];
    if (ed.picked == Picked::Lesson || !page){
        panelHeading("THE LESSON");
        ImGui::PushFont(uiFonts().bold, 15 * s);
        ImGui::TextUnformatted("Title");
        ImGui::PopFont();
        if (undoableInput("##title", &ed.doc.title)) changed();
        ImGui::PushFont(uiFonts().bold, 15 * s);
        ImGui::TextUnformatted("Category");
        ImGui::PopFont();
        if (undoableInput("##category", &ed.doc.category)) changed();
        ImGui::PushFont(uiFonts().bold, 15 * s);
        ImGui::TextUnformatted("Author");
        ImGui::PopFont();
        if (undoableInput("##author", &ed.doc.author)) changed();
        ImGui::PushFont(uiFonts().bold, 15 * s);
        ImGui::TextUnformatted("What it's about");
        ImGui::PopFont();
        if (undoableInput("##description", &ed.doc.description, true, 80 * s)){
            std::replace(ed.doc.description.begin(), ed.doc.description.end(), '\n', ' ');
            changed();
        }
        ImGui::PushFont(uiFonts().bold, 15 * s);
        ImGui::TextUnformatted("Instrument");
        ImGui::PopFont();
        const char* instruments[] = { "Any", "Guitar", "Bass", "Piano" };
        int instrument = (int)ed.doc.instrument;
        ImGui::SetNextItemWidth(-1);
        if (ImGui::Combo("##instrument", &instrument, instruments, 4)){
            beforeChange();
            ed.doc.instrument = (ExerciseInstrument)instrument;
            changed();
        }
        dimText("Whose neck and sound its diagrams use; Learn lists it for that instrument.");
        return;
    }
    if (ed.picked == Picked::Page){
        panelHeading(TextFormat("PAGE %d OF %d", ed.page + 1, (int)ed.doc.pages.size()));
        ImGui::PushFont(uiFonts().bold, 15 * s);
        ImGui::TextUnformatted("Title");
        ImGui::PopFont();
        // Renamed, the blocks sending the student here follow it
        const std::string before = page->title;
        if (undoableInput("##pagetitle", &page->title)){
            for (const BlockPlace& place : lessonBlocks(ed.doc)){
                LessonBlock* block = blockPointer(ed.doc, place);
                for (const char* key : { "help", "ace" })
                    if (!before.empty() && blockValue(*block, key) == before) setBlockValue(*block, key, page->title);
            }
            changed();
        }
        dimText("Over the page, big. Left empty, none.");
        bool aside = page->aside;
        if (ImGui::Checkbox("Set aside, for help", &aside)){
            beforeChange();
            page->aside = aside;
            changed();
        }
        dimText("Skipped on the way through: shown only when a drill missed twice sends the student here, then back.");
        ImGui::Dummy(ImVec2(0, 6 * s));
        if (ImGui::Button("Earlier")) moveChosen(-1);
        ImGui::SameLine();
        if (ImGui::Button("Later")) moveChosen(1);
        ImGui::SameLine();
        if (ImGui::Button("Copy")) duplicateChosen();
        ImGui::SameLine();
        ImGui::BeginDisabled(ed.doc.pages.size() <= 1);
        if (ImGui::Button("Delete")) removeChosen();
        ImGui::EndDisabled();
        ImGui::Dummy(ImVec2(0, 10 * s));
        if (ImGui::Button("The lesson's details", ImVec2(-1, 0))) choose(Picked::Lesson);
        return;
    }
    if (ed.picked == Picked::Section){
        LessonSection& section = page->sections[(size_t)ed.section];
        panelHeading(TextFormat("SECTION %d", ed.section + 1));
        ImGui::PushFont(uiFonts().bold, 15 * s);
        ImGui::TextUnformatted("Columns");
        ImGui::PopFont();
        for (int layout = 0; layout < (int)SectionLayout::Count; layout++){
            if (ImGui::RadioButton(sectionLayoutName((SectionLayout)layout), section.layout == (SectionLayout)layout) && section.layout != (SectionLayout)layout){
                beforeChange();
                setSectionLayout(section, (SectionLayout)layout);
                changed();
            }
        }
        dimText("Fewer columns: the blocks of those gone join the last one's.");
        ImGui::Dummy(ImVec2(0, 6 * s));
        if (ImGui::Button("Up")) moveChosen(-1);
        ImGui::SameLine();
        if (ImGui::Button("Down")) moveChosen(1);
        ImGui::SameLine();
        if (ImGui::Button("Delete")) removeChosen();
        return;
    }
    // A block: what it is, then its settings
    LessonBlock* block = blockPointer(ed.doc, ed.block);
    if (!block){
        choose(Picked::Page);
        return;
    }
    const BlockInfo& info = blockInfo(block->type);
    std::string kind = info.name;
    for (char& c : kind) c = (char)std::toupper((unsigned char)c);
    panelHeading((kind + " BLOCK").c_str());
    ImGui::PushFont(uiFonts().heavy, 22 * s);
    ImGui::TextUnformatted(info.name);
    ImGui::PopFont();
    dimText(info.description);
    if (info.scored){
        ImGui::PushFont(uiFonts().bold, 15 * s);
        ImGui::TextUnformatted("Name");
        ImGui::PopFont();
        if (undoableInput("##blockname", &block->name)){
            if (!block->exerciseLines.empty()) block->exercise.title = block->name.empty() ? "Exercise" : block->name;
            changed();
        }
        dimText("On its card (left empty: the exercise's or song's own)");
    }
    for (const BlockField& field : info.fields){
        ImGui::Dummy(ImVec2(0, 4 * s));
        blockField(*block, field);
        block = blockPointer(ed.doc, ed.block); // (a field may have changed the lesson: found again)
        if (!block) return;
    }
    if (!ed.fieldErrors.empty()){
        ImGui::PushStyleColor(ImGuiCol_Text, uiColorVec(UiColor::Bad));
        ImGui::TextWrapped("%s", ed.fieldErrors.c_str());
        ImGui::PopStyleColor();
    }
    ImGui::Dummy(ImVec2(0, 8 * s));
    if (ImGui::Button("Up")) moveChosen(-1);
    ImGui::SameLine();
    if (ImGui::Button("Down")) moveChosen(1);
    ImGui::SameLine();
    if (ImGui::Button("Copy")) duplicateChosen();
    ImGui::SameLine();
    if (ImGui::Button("Delete")) removeChosen();
}

// --- The page, as it's made ----------------------------------------------------------------------------------------

// The page drawn as the student sees it, what's chosen outlined; a click chooses what's under it
static void canvas(ImVec2 min, ImVec2 max, float s){
    ImDrawList* draw = ImGui::GetWindowDrawList();
    // Its paper: drawn with raylib, under everything (a staff is drawn with raylib too, and must sit on it)
    DrawRectangleRounded({ min.x, min.y, max.x - min.x, max.y - min.y }, 0.02f, 8, themeColor(UiColor::Card));
    if (ed.doc.pages.empty()) return;
    const float width = max.x - min.x - 2 * CANVAS_PAD * s, top = min.y + CANVAS_PAD * s, bottom = max.y - 10 * s;
    const bool hovering = ImGui::IsMouseHoveringRect(min, max);
    if (hovering) ed.scroll -= ImGui::GetIO().MouseWheel * SCROLL_STEP * s;
    ed.scroll = std::clamp(ed.scroll, 0.0f, std::max(0.0f, ed.pageHeight - (bottom - top)));
    draw->PushClipRect(ImVec2(min.x, min.y + 4 * s), ImVec2(max.x, bottom), true);
    PageState state;
    state.editing = true;
    // What each drill runs, for its card: the game's exercise it names, or the one written in it
    const std::vector<BlockPlace> scored = scoredBlocks(ed.doc, ed.page);
    std::vector<ExerciseEntry> written(scored.size()); // (sized first: the cards point into it)
    for (size_t i = 0; i < scored.size(); i++){
        const LessonBlock& block = blockAt(ed.doc, scored[i]);
        const std::string name = blockValue(block, "exercise");
        written[i].exercise = block.exercise;
        state.exercises.push_back(!name.empty() ? findExercise(ed.exercises, ed.builtIn, name)
                                  : block.exerciseLines.empty() ? nullptr : &written[i]);
        state.passed.push_back(false);
    }
    PageEvents events;
    const ImVec2 at(min.x + CANVAS_PAD * s, top - ed.scroll);
    ed.pageHeight = drawLessonPage(ed.doc, ed.page, ed.folder, ed.media, state, events, at, width, s);
    // A click on a neck or the keys marks it there: a note, lit, or nothing again
    if (events.mark.block >= 0 && hovering){
        const BlockPlace place{ ed.page, events.mark.section, events.mark.column, events.mark.block };
        if (LessonBlock* block = blockPointer(ed.doc, place)){
            beforeChange();
            if (events.mark.pitch >= 0) toggleLitKey(*block, events.mark.pitch);
            else cycleNeckPlace(*block, events.mark.string, events.mark.fret);
            ed.fields.erase("dots");
            ed.fields.erase("lit");
            changed();
        }
    }
    // What's chosen, outlined in the accent colour; what's under the mouse, faintly
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    auto inside = [&](const PageRect& rect){ return mouse.x >= rect.min.x && mouse.x <= rect.max.x && mouse.y >= rect.min.y && mouse.y <= rect.max.y; };
    const PageRect* hoveredBlock = nullptr;
    const PageRect* hoveredSection = nullptr;
    for (const PageRect& rect : events.blocks) if (hovering && inside(rect)) hoveredBlock = &rect;
    for (const PageRect& rect : events.sections) if (hovering && inside(rect)) hoveredSection = &rect;
    const float grow = 6 * s;
    auto outline = [&](const PageRect& rect, ImU32 color, float thickness){
        draw->AddRect(ImVec2(rect.min.x - grow, rect.min.y - grow), ImVec2(rect.max.x + grow, rect.max.y + grow), color, 6 * s, 0, thickness);
    };
    for (const PageRect& rect : events.sections){
        const bool chosen = ed.picked == Picked::Section && rect.section == ed.section;
        if (chosen || (hoveredSection == &rect && !hoveredBlock)){
            outline(rect, uiColor(UiColor::Accent, chosen ? 0.9f : 0.35f), chosen ? 2 * s : 1.0f);
            const LessonSection& section = ed.doc.pages[(size_t)ed.page].sections[(size_t)rect.section];
            draw->AddText(uiFonts().mono, 11 * s, ImVec2(rect.min.x - grow, rect.min.y - grow - 15 * s), uiColor(UiColor::Accent, chosen ? 1.0f : 0.6f),
                          TextFormat("SECTION  ·  %s", sectionLayoutName(section.layout)));
        }
    }
    for (const PageRect& rect : events.blocks){
        const bool chosen = ed.picked == Picked::Block && rect.section == ed.block.section && rect.column == ed.block.column && rect.block == ed.block.block;
        if (chosen) outline(rect, uiColor(UiColor::Accent), 2 * s);
        else if (hoveredBlock == &rect) outline(rect, uiColor(UiColor::Ink, 0.3f), 1.0f);
    }
    // Dragging: where it would go, a line between the blocks of the column under the mouse
    const bool dragging = ed.drag.kind == Drag::Moving || ed.drag.kind == Drag::Adding;
    bool dropping = false;
    BlockPlace target;
    if (dragging && hovering){
        for (const PageRect& column : events.columns){
            if (mouse.x < column.min.x - grow || mouse.x > column.max.x + grow || mouse.y < column.min.y - 2 * grow || mouse.y > column.max.y + 2 * grow) continue;
            target = { ed.page, column.section, column.column, 0 };
            float lineY = column.min.y - 8 * s;
            for (const PageRect& rect : events.blocks){
                if (rect.section != column.section || rect.column != column.column) continue;
                if (mouse.y > (rect.min.y + rect.max.y) / 2){ // below its middle: after it
                    target.block = rect.block + 1;
                    lineY = rect.max.y + 8 * s;
                } else if (target.block == rect.block) lineY = rect.min.y - 8 * s;
            }
            dropping = true;
            draw->AddLine(ImVec2(column.min.x, lineY), ImVec2(column.max.x, lineY), uiColor(UiColor::Accent), 3 * s);
            draw->AddCircleFilled(ImVec2(column.min.x, lineY), 5 * s, uiColor(UiColor::Accent));
            draw->AddCircleFilled(ImVec2(column.max.x, lineY), 5 * s, uiColor(UiColor::Accent));
            break;
        }
        // Near the top or bottom of the page, it scrolls on
        if (mouse.y < top + 40 * s) ed.scroll -= EDGE_SCROLL * s * ImGui::GetIO().DeltaTime;
        if (mouse.y > bottom - 40 * s) ed.scroll += EDGE_SCROLL * s * ImGui::GetIO().DeltaTime;
    }
    draw->PopClipRect();
    if (dragging){ // what's being dragged, by the mouse
        const char* label = ed.drag.kind == Drag::Adding ? blockInfo(ed.drag.type).name : "Move here";
        const ImVec2 extent = uiFonts().bold->CalcTextSizeA(14 * s, FLT_MAX, 0.0f, label);
        ImDrawList* front = ImGui::GetForegroundDrawList();
        const ImVec2 a(mouse.x + 14 * s, mouse.y + 10 * s);
        front->AddRectFilled(a, ImVec2(a.x + extent.x + 20 * s, a.y + extent.y + 10 * s), uiColor(UiColor::Accent, dropping ? 0.95f : 0.5f), 6 * s);
        front->AddText(uiFonts().bold, 14 * s, ImVec2(a.x + 10 * s, a.y + 5 * s), uiColor(UiColor::Background), label);
    }
    // Let go: dropped there (a page with no section gets one), or nowhere
    if (dragging && ImGui::IsMouseReleased(ImGuiMouseButton_Left)){
        if (dropping && target.section < 0) target = { ed.page, 0, 0, 0 }; // the empty page's own room: addBlock gives it a section
        if (dropping && ed.drag.kind == Drag::Adding) addBlock(ed.drag.type, target);
        else if (dropping && target.section >= 0){
            beforeChange();
            const BlockPlace landed = moveBlock(ed.doc, ed.drag.from, target);
            choose(Picked::Block, landed.section, landed);
            changed();
        }
        ed.drag = Drag{};
        return;
    }
    // A click chooses: a block (and may start moving it), else its section, else the page
    if (hovering && ImGui::IsMouseClicked(ImGuiMouseButton_Left)){
        if (hoveredBlock){
            choose(Picked::Block, hoveredBlock->section, { ed.page, hoveredBlock->section, hoveredBlock->column, hoveredBlock->block });
            ed.drag.kind = Drag::Pending;
            ed.drag.from = ed.block;
            ed.drag.start = mouse;
        } else if (hoveredSection) choose(Picked::Section, hoveredSection->section);
        else choose(Picked::Page);
    }
    if (ed.drag.kind == Drag::Pending){
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) ed.drag = Drag{};
        else if (std::hypot(mouse.x - ed.drag.start.x, mouse.y - ed.drag.start.y) > DRAG_START * s) ed.drag.kind = Drag::Moving;
    }
}

// Files dropped on the maker (pictures, sounds, videos, songs): copied into the lesson's folder, each a block of its
// own after what's chosen
static void takeDroppedFiles(){
    if (!IsFileDropped()) return;
    FilePathList dropped = LoadDroppedFiles();
    std::vector<std::string> paths(dropped.paths, dropped.paths + dropped.count);
    UnloadDroppedFiles(dropped);
    if (ed.builtIn){
        ed.status = "Built in: save first, to have your own copy to add files to";
        return;
    }
    for (const std::string& path : paths){
        std::string extension = fs::path(path).extension().string();
        for (char& c : extension) c = (char)std::tolower((unsigned char)c);
        BlockType type = BlockType::Count;
        for (BlockType candidate : { BlockType::Image, BlockType::Audio, BlockType::Video, BlockType::Play }){
            const BlockField* file = findBlockField(candidate, "file");
            if (file && std::find(file->choices.begin(), file->choices.end(), extension) != file->choices.end()) type = candidate;
        }
        if (type == BlockType::Count){
            ed.status = fs::path(path).filename().string() + ": not a picture (.png, .jpg), a sound (.wav, .flac, .mp3), a video (.mpg) or a song (.chart)";
            continue;
        }
        // Into the folder, under a name of its own there
        fs::path destination = fs::path(ed.folder) / fs::path(path).filename();
        for (int n = 2; fs::exists(destination) && !fs::equivalent(destination, path); n++)
            destination = fs::path(ed.folder) / (fs::path(path).stem().string() + " (" + std::to_string(n) + ")" + fs::path(path).extension().string());
        std::error_code ec;
        if (!fs::exists(destination)) fs::copy_file(path, destination, ec);
        // A song's audio goes along with it
        if (!ec && type == BlockType::Play){
            Chart chart;
            std::string error;
            if (loadChart(path, chart, error) && !chart.audioFile.empty()){
                const fs::path audio = fs::path(path).parent_path() / chart.audioFile;
                if (fs::exists(audio) && !fs::exists(fs::path(ed.folder) / chart.audioFile)) fs::copy_file(audio, fs::path(ed.folder) / chart.audioFile, ec);
            }
        }
        if (ec){
            ed.status = "Couldn't copy " + fs::path(path).filename().string() + ": " + ec.message();
            continue;
        }
        refreshFolderFiles();
        if (ed.doc.pages.empty()) continue;
        addBlock(type, placeForNew());
        if (LessonBlock* block = blockPointer(ed.doc, ed.block)){
            setBlockValue(*block, "file", destination.filename().string());
            changed();
        }
    }
}

// --- Screens ----------------------------------------------------------------------------------------------------------

// The card on the right of the list: the selected lesson, or the new lesson's name field
static void lessonCard(bool naming, const LessonEntry* entry, float s){
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const UiFonts& fonts = uiFonts();
    const float width = ImGui::GetWindowWidth(), height = ImGui::GetWindowHeight();
    const ImVec2 card(width * 0.58f, height * 0.25f);
    const float cardWidth = width * 0.35f, pad = 26 * s, inner = cardWidth - 2 * pad;
    const std::string title = naming ? "New lesson" : entry->lesson.title;
    const std::string detail = naming ? "" : TextFormat("%d %s", (int)entry->doc.pages.size(), entry->doc.pages.size() == 1 ? "page" : "pages");
    const std::string hint = naming ? lessonTemplates()[(size_t)ed.newTemplate].description : (entry->builtIn ? "Built in: saving makes your own copy" : "Yours");
    const std::string& error = naming ? ed.listError : entry->error;
    const float titleHeight = fonts.bold->CalcTextSizeA(24 * s, FLT_MAX, inner, title.c_str()).y;
    const float errorHeight = error.empty() ? 0.0f : fonts.text->CalcTextSizeA(15 * s, FLT_MAX, inner, error.c_str()).y + 12 * s;
    const float fieldHeight = naming ? 3 * (ImGui::GetFrameHeight() + 10 * s) + 4 * s : 0.0f;
    const float cardHeight = pad * 2 + titleHeight + 12 * s + fieldHeight + (naming ? ImGui::GetFrameHeight() + 10 * s : 0.0f) + (detail.empty() ? 0 : 18 * s) + 10 * s + 16 * s + errorHeight;
    draw->AddRectFilled(ImVec2(card.x, card.y + 3 * s), ImVec2(card.x + cardWidth, card.y + cardHeight + 3 * s), uiColor(UiColor::Ink, 0.04f), 10 * s);
    draw->AddRectFilled(card, ImVec2(card.x + cardWidth, card.y + cardHeight), uiColor(UiColor::Card), 10 * s);
    float x = card.x + pad, y = card.y + pad;
    draw->AddText(fonts.bold, 24 * s, ImVec2(x, y), uiColor(UiColor::Ink), title.c_str(), nullptr, inner);
    y += titleHeight + 12 * s;
    if (naming){
        // Confirming "New lesson" puts the keyboard in the field; Enter there creates, Esc steps back to the list
        ImGui::SetCursorScreenPos(ImVec2(x, y));
        ImGui::SetNextItemWidth(inner);
        if (ed.focusName) ImGui::SetKeyboardFocusHere();
        ed.focusName = false;
        if (ImGui::InputTextWithHint("##name", "Its name", &ed.newLessonName, ImGuiInputTextFlags_EnterReturnsTrue)) createLesson();
        if (ImGui::IsItemEdited()) ed.listError.clear(); // the complaint was about the old name
        // What it starts from, and what it's played on
        ImGui::SetCursorScreenPos(ImVec2(x, y + ImGui::GetFrameHeight() + 10 * s));
        ImGui::SetNextItemWidth(inner);
        if (ImGui::BeginCombo("##template", lessonTemplates()[(size_t)ed.newTemplate].name)){
            for (int i = 0; i < (int)lessonTemplates().size(); i++){
                if (ImGui::Selectable(lessonTemplates()[(size_t)i].name, i == ed.newTemplate)) ed.newTemplate = i;
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", lessonTemplates()[(size_t)i].description);
            }
            ImGui::EndCombo();
        }
        ImGui::SetCursorScreenPos(ImVec2(x, y + 2 * (ImGui::GetFrameHeight() + 10 * s)));
        ImGui::SetNextItemWidth(inner);
        const char* instruments[] = { "Any instrument", "Guitar", "Bass", "Piano" };
        ImGui::Combo("##instrument", &ed.newInstrument, instruments, 4);
        y += fieldHeight;
        ImGui::SetCursorScreenPos(ImVec2(x, y));
        if (ImGui::Button("Create", ImVec2(inner, 0))) createLesson();
        y += ImGui::GetFrameHeight() + 10 * s;
    }
    if (!detail.empty()){
        draw->AddText(fonts.bold, 18 * s, ImVec2(x, y), uiColor(UiColor::Accent), detail.c_str());
        y += 18 * s;
    }
    y += 10 * s;
    draw->AddText(fonts.text, 16 * s, ImVec2(x, y), uiColor(UiColor::Dim), hint.c_str(), nullptr, inner);
    y += 16 * s;
    if (!error.empty()) draw->AddText(fonts.text, 15 * s, ImVec2(x, y + 12 * s), uiColor(UiColor::Bad), error.c_str(), nullptr, inner);
}

static LessonEditorChoice lessonList(){
    static MenuList list;
    LessonEditorChoice choice = LessonEditorChoice::None;
    beginMenu("Lesson maker");
    const float s = menuScale();
    menuScreenTitle("Lesson maker", s);
    // The lessons, then a new one, the folder and Back. One with something to fix still opens, to fix it here.
    std::vector<MenuRow> rows;
    for (const LessonEntry& entry : ed.lessons){
        MenuRow row;
        row.label = entry.lesson.title;
        row.detail = entry.builtIn ? "built in" : "yours";
        row.note = entry.error;
        rows.push_back(row);
    }
    const int newLesson = (int)rows.size(), openLessons = newLesson + 1, back = newLesson + 2;
    rows.push_back(actionRow("New lesson"));
    rows.push_back(actionRow("Open lessons folder"));
    rows.push_back(actionRow("Back", "Esc"));
    const float width = ImGui::GetWindowWidth(), height = ImGui::GetWindowHeight();
    const bool typing = ImGui::GetIO().WantTextInput; // before the field is drawn: this frame's keys belong to it
    const int confirmed = menuList(list, rows, { ImVec2(width * 0.07f, height * 0.25f), width * 0.45f, height * 0.66f - 20 * s, s });
    if (confirmed == newLesson) ed.focusName = true;
    if (confirmed >= 0 && confirmed < newLesson) openLesson(ed.lessons[(size_t)confirmed].folder, ed.lessons[(size_t)confirmed].builtIn);
    if (confirmed == openLessons){
        std::error_code ec;
        fs::create_directories(ed.setup.userLessons, ec);
        openFolder(ed.setup.userLessons);
    }
    if (confirmed == back || (ImGui::IsKeyPressed(ImGuiKey_Escape) && !typing)) choice = LessonEditorChoice::Back;
    if (ed.editing){ // a lesson just opened: the maker draws from the next frame
        ImGui::End();
        return choice;
    }
    if (list.selected == newLesson) lessonCard(true, nullptr, s);
    else if (list.selected >= 0 && list.selected < newLesson) lessonCard(false, &ed.lessons[(size_t)list.selected], s);
    else if (!ed.listError.empty()){
        ImGui::SetCursorPos(ImVec2(width * 0.07f, height * 0.17f + 20 * s));
        ImGui::TextColored(uiColorVec(UiColor::Bad), "%s", ed.listError.c_str());
    }
    menuScreenHint(typing ? "Enter  create    Esc  cancel"
                          : list.selected == newLesson ? "Up/Down  choose    Enter  name it    Esc  back"
                                                       : "Up/Down  choose    Enter  make    Esc  back", s);
    ImGui::End();
    return choice;
}

static void leaveLesson(){
    releasePageMedia(ed.media);
    ed.editing = false;
    refreshLists(); // a new copy, a new title or a fixed problem shows in the list
}

// The lesson played from the page shown, as a student would (its progress apart): Esc comes back to making it
static void startTryout(){
    if (ed.doc.pages.empty()) return;
    releasePageMedia(ed.media);
    LessonEntry entry;
    entry.folder = ed.folder;
    entry.id = "tryout-" + fs::path(ed.folder).filename().string();
    entry.builtIn = ed.builtIn;
    entry.version = LESSON_DOC_VERSION;
    entry.doc = ed.doc;
    const std::string progress = (fs::temp_directory_path() / "lahn-lesson-tryout.txt").string();
    std::error_code ec;
    fs::remove(progress, ec); // nothing passed yet
    ed.tryout = learnTryLesson(entry, ed.page, progress);
}

static void tryingOut(){
    beginMenu("Lesson tryout");
    ed.tryout->update();
    if (ed.tryout) ed.tryout->draw();
    // Above it all: what this is, and the way back
    const float s = menuScale();
    ImDrawList* draw = ImGui::GetForegroundDrawList();
    const char* note = "TRYING IT OUT  ·  Esc: back to making it";
    const ImVec2 extent = uiFonts().mono->CalcTextSizeA(12 * s, FLT_MAX, 0.0f, note);
    const float x = ImGui::GetIO().DisplaySize.x - extent.x - 30 * s, y = ImGui::GetIO().DisplaySize.y - 30 * s;
    draw->AddRectFilled(ImVec2(x - 10 * s, y - 6 * s), ImVec2(x + extent.x + 10 * s, y + extent.y + 6 * s), uiColor(UiColor::Accent, 0.9f), 6 * s);
    draw->AddText(uiFonts().mono, 12 * s, ImVec2(x, y), uiColor(UiColor::Background), note);
    ImGui::End();
    if (ed.tryout->wantsToLeave() || (ImGui::IsKeyPressed(ImGuiKey_Escape, false) && !ed.tryout->back())) ed.tryout.reset();
}

static void lessonEditing(){
    ImGuiIO& io = ImGui::GetIO();
    beginMenu("Lesson maker editing");
    const float s = menuScale(), width = ImGui::GetWindowWidth(), height = ImGui::GetWindowHeight();
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const UiFonts& fonts = uiFonts();
    bool requestBack = false;

    // The top bar: Back, the lesson's title (and whether it's saved, and playable), Undo, Redo, Try, Save
    const float barY = 16 * s, left = PANEL_GAP * s;
    float pillWidth = 0.0f;
    if (pill(ImVec2(left, barY), "Back", false, true, s, &pillWidth)) requestBack = true;
    const float titleX = left + pillWidth + 18 * s;
    const std::string title = (ed.doc.title.empty() ? std::string("(no title)") : ed.doc.title) + (ed.dirty ? "  *" : "");
    draw->AddText(fonts.heavy, 22 * s, ImVec2(titleX, barY - 2 * s), uiColor(UiColor::Ink), title.c_str());
    const std::string state = !ed.status.empty() ? ed.status : ed.problem.empty() ? "Ready to be played" : "Not ready yet: " + ed.problem;
    draw->AddText(fonts.text, 13 * s, ImVec2(titleX, barY + 24 * s),
                  uiColor(!ed.status.empty() ? UiColor::Dim : ed.problem.empty() ? UiColor::Good : UiColor::Bad), state.c_str());
    float right = width - left;
    auto barButton = [&](const char* label, bool lit, bool live){
        const ImVec2 extent = fonts.bold->CalcTextSizeA(15 * s, FLT_MAX, 0.0f, label);
        right -= extent.x + 28 * s;
        const bool clicked = pill(ImVec2(right, barY), label, lit, live, s);
        right -= 8 * s;
        return clicked;
    };
    if (barButton("Save", ed.dirty, true)) saveEditedLesson();
    if (barButton("Try this page", false, !ed.doc.pages.empty())) startTryout();
    if (barButton("Redo", false, !ed.redos.empty())) redo();
    if (barButton("Undo", false, !ed.undos.empty())) undo();

    // The three panels: the outline, the page, the inspector
    const float top = TOP_BAR * s, bottom = height - PANEL_GAP * s;
    const float outlineRight = left + OUTLINE_WIDTH * s, inspectorLeft = width - left - INSPECTOR_WIDTH * s;
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6 * s);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 8 * s);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12 * s, 10 * s));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, uiColorVec(UiColor::Card));
    ImGui::SetCursorScreenPos(ImVec2(left, top));
    // (the panels' fields and buttons in the text font, at the size of the rest of the screen's small print)
    ImGui::PushFont(fonts.text, 15 * s);
    ImGui::BeginChild("Outline", ImVec2(outlineRight - left, bottom - top), ImGuiChildFlags_AlwaysUseWindowPadding);
    outlinePanel(s);
    ImGui::EndChild();
    ImGui::SetCursorScreenPos(ImVec2(inspectorLeft, top));
    ImGui::BeginChild("Inspector", ImVec2(width - left - inspectorLeft, bottom - top), ImGuiChildFlags_AlwaysUseWindowPadding);
    inspectorPanel(s);
    ImGui::EndChild();
    ImGui::PopFont();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(3);
    canvas(ImVec2(outlineRight + PANEL_GAP * s, top), ImVec2(inspectorLeft - PANEL_GAP * s, bottom), s);
    takeDroppedFiles();
    // Let go anywhere but the page: nothing's dropped
    if ((ed.drag.kind == Drag::Moving || ed.drag.kind == Drag::Adding) && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) ed.drag = Drag{};

    // The keys (not while typing): Ctrl+S save, Ctrl+Z undo, Ctrl+Y redo, Delete, Ctrl+D copy, Alt+Up/Down move,
    // Page Up/Down the pages, T try it, Esc back
    if (!ImGui::IsPopupOpen(UNSAVED_POPUP) && !io.WantTextInput){
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S)) saveEditedLesson();
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Z)) io.KeyShift ? redo() : undo();
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Y)) redo();
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_D)) duplicateChosen();
        if (ImGui::IsKeyPressed(ImGuiKey_Delete)) removeChosen();
        if (io.KeyAlt && ImGui::IsKeyPressed(ImGuiKey_UpArrow)) moveChosen(-1);
        if (io.KeyAlt && ImGui::IsKeyPressed(ImGuiKey_DownArrow)) moveChosen(1);
        if (ImGui::IsKeyPressed(ImGuiKey_PageUp)) showPage(ed.page - 1);
        if (ImGui::IsKeyPressed(ImGuiKey_PageDown)) showPage(ed.page + 1);
        if (!io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_T, false)) startTryout();
        if (ImGui::IsKeyPressed(ImGuiKey_Escape)) requestBack = true;
    }
    if (requestBack){
        if (ed.dirty) ImGui::OpenPopup(UNSAVED_POPUP);
        else leaveLesson();
    }
    // Leaving with unsaved changes asks first
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::PushFont(fonts.text, 16 * s);
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
    ImGui::PopFont();
    ImGui::Dummy(ImVec2(1, 1)); // the cursor was moved about: an item after it
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
    if (ed.tryout){
        tryingOut();
        return LessonEditorChoice::None;
    }
    if (ed.editing){
        lessonEditing();
        return LessonEditorChoice::None;
    }
    return lessonList();
}

void closeLessonEditor(){
    if (!ed.active) return;
    ed.tryout.reset();
    releasePageMedia(ed.media);
    ed.active = false;
    ed.editing = false;
}
