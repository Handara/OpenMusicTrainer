#include "core/lessondoc.h"

#include "core/chart.h"
#include "core/files.h"
#include "core/music.h"
#include "core/notation.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>

namespace fs = std::filesystem;

// --- What each block takes ----------------------------------------------------------------------------------------

static const std::vector<std::string> IMAGE_FILES = { ".png", ".jpg", ".jpeg" };
static const std::vector<std::string> SOUND_FILES = { ".wav", ".flac", ".mp3" };
static const std::vector<std::string> VIDEO_FILES = { ".mpg", ".mpeg" }; // MPEG-1: see the video player
static const std::vector<std::string> CHART_FILES = { ".chart" };

// What every scored block takes
static BlockField gateField(const char* standard = "yes"){
    return { "gate", "Must pass", FieldKind::Toggle, "Passed before going on; or not, and it's there to practise", standard };
}
// Where a scored block may send the student: to help, after missing it; ahead, after acing it
static BlockField helpField(){
    return { "help", "When it's missed twice", FieldKind::Page, "A page of help to show (one set aside for it is skipped otherwise)" };
}
static BlockField aceField(){
    return { "ace", "When it's aced", FieldKind::Page, "Every note right the first try: straight on to this page" };
}
// In a course: its number in its chapter, which its progress is kept by (so words added before it don't move it)
static BlockField idField(){
    return { "id", "Number", FieldKind::Number, "Its number in its course chapter: the course keeps its progress by it", "", {}, 1, 9999 };
}
static BlockField goalField(const char* description, int max){
    return { "goal", "Goal", FieldKind::Number, description, "", {}, 1, max };
}

const std::vector<BlockInfo>& blockInfos(){
    static const std::vector<BlockInfo> infos = {
        { BlockType::Text, "text", "Text", "Paragraphs to read", BlockGroup::Show, false, {
            { "text", "Text", FieldKind::Paragraphs, "A paragraph each", "", {}, 0, 0, true },
        } },
        { BlockType::Heading, "heading", "Heading", "A heading, over what follows", BlockGroup::Show, false, {
            { "text", "Heading", FieldKind::Text, "Its words", "", {}, 0, 0, true },
        } },
        { BlockType::Callout, "callout", "Callout", "A tip, something to remember, or a warning, in a box of its own", BlockGroup::Show, false, {
            { "style", "Kind", FieldKind::Choice, "A tip, something to remember, or something to be careful about", "tip",
              { "tip", "remember", "careful" } },
            { "text", "Text", FieldKind::Paragraphs, "A paragraph each", "", {}, 0, 0, true },
        } },
        { BlockType::Reveal, "reveal", "Reveal", "Something hidden until it's asked for: an answer, a hint", BlockGroup::Show, false, {
            { "label", "Button", FieldKind::Text, "What the button says", "Show the answer" },
            { "text", "Hidden text", FieldKind::Paragraphs, "A paragraph each, shown once the button's pressed", "", {}, 0, 0, true },
        } },
        { BlockType::Fretboard, "fretboard", "Neck", "Part of the neck, with notes marked on it (click one to hear it)", BlockGroup::Show, false, {
            { "frets", "Frets", FieldKind::Span, "The first and last fret shown (0: the open strings too)", "0 5", {}, 0, 24 },
            { "dots", "Notes", FieldKind::Places, "Notes marked: string:fret, 1 = the lowest string; a label after another colon (6:1:F, 5:3:2)" },
            { "lit", "Lit notes", FieldKind::Places, "Notes marked in the accent colour, to stand out" },
            { "labels", "On the notes", FieldKind::Choice, "Each note's name, or nothing but its own label", "names", { "names", "none" } },
            { "instrument", "Instrument", FieldKind::Choice, "Whose neck: the lesson's instrument, or one of these", "lesson", { "lesson", "guitar", "bass" } },
            { "caption", "Caption", FieldKind::Text, "Under it" },
        } },
        { BlockType::Keyboard, "keyboard", "Keyboard", "Piano keys, some lit (click one to hear it)", BlockGroup::Show, false, {
            { "from", "From", FieldKind::Note, "The lowest note shown (the keyboard starts at a C)", "C4" },
            { "to", "To", FieldKind::Note, "The highest note shown", "C5" },
            { "lit", "Lit keys", FieldKind::Notes, "Keys lit, by their notes: C4 E4 G4" },
            { "labels", "On the keys", FieldKind::Choice, "The lit keys' names, or nothing", "names", { "names", "none" } },
            { "caption", "Caption", FieldKind::Text, "Under it" },
        } },
        { BlockType::Staff, "staff", "Staff", "Notes written on a staff, to read (and hear)", BlockGroup::Show, false, {
            { "notes", "Notes", FieldKind::Notes, "The notes, in order, a beat each (up to 8 shown): E4 F4 G4", "", {}, 0, 0, true },
            { "key", "Key", FieldKind::Key, "Its key signature: C major, G major, E minor...", "C major" },
            { "listen", "Listen button", FieldKind::Toggle, "A button that plays the notes", "yes" },
            { "caption", "Caption", FieldKind::Text, "Under it" },
        } },
        { BlockType::Image, "image", "Picture", "A picture from the lesson's folder", BlockGroup::Show, false, {
            { "file", "Picture", FieldKind::File, "A .png or .jpg in the lesson's folder", "", IMAGE_FILES, 0, 0, true },
            { "caption", "Caption", FieldKind::Text, "Under it", "" },
        } },
        { BlockType::Audio, "audio", "Sound", "A recording to listen to", BlockGroup::Hear, false, {
            { "file", "Sound", FieldKind::File, "A .wav, .flac or .mp3 in the lesson's folder", "", SOUND_FILES, 0, 0, true },
            { "caption", "Caption", FieldKind::Text, "Under it", "" },
        } },
        { BlockType::Video, "video", "Video", "A video to watch", BlockGroup::Show, false, {
            { "file", "Video", FieldKind::File, "An MPEG-1 video (.mpg) in the lesson's folder", "", VIDEO_FILES, 0, 0, true },
            { "caption", "Caption", FieldKind::Text, "Under it", "" },
        } },
        { BlockType::Exercise, "exercise", "Exercise", "A drill, a quiz or a game: one of the game's exercises, or one written here",
          BlockGroup::Play, true, {
            { "exercise", "Exercise", FieldKind::Exercise, "One of the game's exercises (named by its file name), or one made here" },
            goalField("Clean passes for a drill, right answers in a row for intervals (left out: the usual)", 999),
            gateField(), helpField(), aceField(), idField(),
        } },
        { BlockType::Play, "play", "Song", "A song (or a few bars of one) to play along to", BlockGroup::Play, true, {
            { "song", "Song", FieldKind::Song, "One of the game's songs" },
            { "file", "Or a file", FieldKind::File, "Or a .chart in the lesson's folder, with its audio", "", CHART_FILES },
            { "part", "Part", FieldKind::Number, "Which of its parts is played (1: its first)", "1", {}, 1, 16 },
            { "bars", "Bars", FieldKind::Span, "Only these bars, as in practice mode (5 8); left out: all of it", "", {}, 1, 9999 },
            { "tempo", "Tempo", FieldKind::Number, "Its speed, in percent of the song's own (time-stretched, its pitch kept)", "100", {}, 30, 100 },
            goalField("The share of notes to hit, in percent (left out: 80)", 100),
            gateField(), helpField(), aceField(), idField(),
        } },
        { BlockType::Practice, "practice", "Practice", "A drill made as it's played: from the notes the student misses most, or from this lesson's",
          BlockGroup::Smart, true, {
            { "from", "Its notes", FieldKind::Choice, "The student's weakest lately (on the lesson's instrument), or the ones this lesson uses",
              "weak", { "weak", "lesson" } },
            { "as", "Played as", FieldKind::Choice, "Notes to play (no clock), or read to a beat", "notes", { "notes", "reading" } },
            { "count", "How many notes", FieldKind::Number, "Taken into it, at most", "6", {}, 2, 12 },
            goalField("Runs passed, or clean passes (left out: the usual)", 99),
            gateField("no"), idField(),
        } },
    };
    return infos;
}

const BlockInfo& blockInfo(BlockType type){
    for (const BlockInfo& info : blockInfos()) if (info.type == type) return info;
    return blockInfos().front();
}

const BlockInfo* findBlockInfo(const std::string& id){
    for (const BlockInfo& info : blockInfos()) if (id == info.id) return &info;
    return nullptr;
}

const BlockField* findBlockField(BlockType type, const std::string& key){
    for (const BlockField& field : blockInfo(type).fields) if (key == field.key) return &field;
    return nullptr;
}

// --- Block values -------------------------------------------------------------------------------------------------

std::string blockValue(const LessonBlock& block, const std::string& key){
    for (const auto& [name, value] : block.values) if (name == key) return value;
    const BlockField* field = findBlockField(block.type, key);
    return field ? field->standard : "";
}

std::vector<std::string> blockValues(const LessonBlock& block, const std::string& key){
    std::vector<std::string> found;
    for (const auto& [name, value] : block.values) if (name == key) found.push_back(value);
    return found;
}

void setBlockValue(LessonBlock& block, const std::string& key, const std::string& value){
    setBlockValues(block, key, value.empty() ? std::vector<std::string>{} : std::vector<std::string>{ value });
}

void setBlockValues(LessonBlock& block, const std::string& key, const std::vector<std::string>& values){
    // In the place the first one had, so a file keeps its order as it's edited
    auto first = std::find_if(block.values.begin(), block.values.end(), [&](const auto& value){ return value.first == key; });
    size_t at = (size_t)(first - block.values.begin());
    block.values.erase(std::remove_if(block.values.begin(), block.values.end(), [&](const auto& value){ return value.first == key; }),
                       block.values.end());
    at = std::min(at, block.values.size());
    for (const std::string& value : values) block.values.insert(block.values.begin() + (std::ptrdiff_t)at++, { key, value });
}

bool blockScored(const LessonBlock& block){
    return blockInfo(block.type).scored;
}

bool blockGates(const LessonBlock& block){
    return blockScored(block) && blockValue(block, "gate") != "no";
}

int blockGoal(const LessonBlock& block){
    return blockScored(block) ? std::atoi(blockValue(block, "goal").c_str()) : 0;
}

std::vector<NeckPlace> readNeckPlaces(const std::string& value){
    std::vector<NeckPlace> places;
    std::istringstream words(value);
    std::string word;
    while (words >> word){
        NeckPlace place;
        char label[64] = "";
        if (std::sscanf(word.c_str(), "%d:%d:%63s", &place.string, &place.fret, label) < 2 || place.string < 1) continue;
        place.string--;
        place.label = label;
        places.push_back(place);
    }
    return places;
}

std::vector<int> readNotes(const std::string& value){
    std::vector<int> pitches;
    std::istringstream words(value);
    std::string word;
    int pitch;
    while (words >> word) if (parseNoteName(word, pitch)) pitches.push_back(pitch);
    return pitches;
}

// --- Sections -----------------------------------------------------------------------------------------------------

struct LayoutInfo {
    SectionLayout layout;
    const char* id;
    const char* name;
    std::vector<float> shares;
};
static const std::vector<LayoutInfo>& layouts(){
    static const std::vector<LayoutInfo> all = {
        { SectionLayout::Single, "single", "One column", { 1.0f } },
        { SectionLayout::Halves, "halves", "Two halves", { 0.5f, 0.5f } },
        { SectionLayout::WideNarrow, "wide-narrow", "Wide, narrow", { 0.62f, 0.38f } },
        { SectionLayout::NarrowWide, "narrow-wide", "Narrow, wide", { 0.38f, 0.62f } },
        { SectionLayout::Thirds, "thirds", "Three columns", { 1.0f / 3, 1.0f / 3, 1.0f / 3 } },
    };
    return all;
}
static const LayoutInfo& layoutInfo(SectionLayout layout){
    for (const LayoutInfo& info : layouts()) if (info.layout == layout) return info;
    return layouts().front();
}

const char* sectionLayoutId(SectionLayout layout){ return layoutInfo(layout).id; }
const char* sectionLayoutName(SectionLayout layout){ return layoutInfo(layout).name; }
int sectionColumns(SectionLayout layout){ return (int)layoutInfo(layout).shares.size(); }
std::vector<float> sectionShares(SectionLayout layout){ return layoutInfo(layout).shares; }

LessonSection makeSection(SectionLayout layout){
    LessonSection section;
    section.layout = layout;
    section.columns.resize((size_t)sectionColumns(layout));
    return section;
}

int findPage(const LessonDoc& doc, const std::string& title){
    for (int p = 0; p < (int)doc.pages.size(); p++) if (doc.pages[(size_t)p].title == title) return p;
    return -1;
}

std::vector<BlockPlace> lessonBlocks(const LessonDoc& doc){
    std::vector<BlockPlace> places;
    for (int p = 0; p < (int)doc.pages.size(); p++){
        const LessonPage& page = doc.pages[(size_t)p];
        for (int s = 0; s < (int)page.sections.size(); s++){
            const LessonSection& section = page.sections[(size_t)s];
            for (int c = 0; c < (int)section.columns.size(); c++)
                for (int b = 0; b < (int)section.columns[(size_t)c].size(); b++) places.push_back({ p, s, c, b });
        }
    }
    return places;
}

const LessonBlock& blockAt(const LessonDoc& doc, const BlockPlace& place){
    return doc.pages[(size_t)place.page].sections[(size_t)place.section].columns[(size_t)place.column][(size_t)place.block];
}

// --- Reading ------------------------------------------------------------------------------------------------------

static std::string lowercaseExtension(const std::string& file){
    std::string extension = fs::path(file).extension().string();
    for (char& c : extension) c = (char)std::tolower((unsigned char)c);
    return extension;
}

static const char* instrumentId(ExerciseInstrument instrument){
    switch (instrument){
        case ExerciseInstrument::Guitar: return "guitar";
        case ExerciseInstrument::Bass: return "bass";
        case ExerciseInstrument::Piano: return "piano";
        default: return "any";
    }
}

// The version a lesson's text says it is (0: none)
static int versionOf(const std::string& text){
    std::istringstream lines(text);
    std::string line;
    while (std::getline(lines, line)){
        std::istringstream ss(line);
        std::string key;
        if (!(ss >> key) || key[0] == '#') continue;
        int version = 0;
        if (key == "version" && ss >> version) return version;
        if (key == "version") return -1;
    }
    return 0;
}

// A setting's value checked against what the block's field takes; "" when it's fine, else what's wrong
static std::string checkValue(const BlockField& field, const std::string& value){
    if (value.empty()) return std::string("'") + field.key + "' needs a value";
    switch (field.kind){
        case FieldKind::Number: {
            std::istringstream number(value);
            int n = 0;
            bool ok = (bool)(number >> n);
            if (ok && number.peek() == '%') number.get(); // a song's goal: "90" or "90%"
            if (!ok || !(number >> std::ws).eof() || n < field.min || n > field.max)
                return std::string("'") + field.key + "' is a number from " + std::to_string(field.min) + " to " + std::to_string(field.max);
            return "";
        }
        case FieldKind::Toggle:
            return value == "yes" || value == "no" ? "" : std::string("'") + field.key + "' is yes or no";
        case FieldKind::Choice: {
            if (std::find(field.choices.begin(), field.choices.end(), value) != field.choices.end()) return "";
            std::string list;
            for (const std::string& choice : field.choices) list += (list.empty() ? "" : ", ") + choice;
            return std::string("'") + field.key + "' is one of: " + list;
        }
        case FieldKind::Note: {
            int pitch;
            return parseNoteName(value, pitch) ? "" : "'" + value + "' isn't a note: write it like E4, F#3, Bb2";
        }
        case FieldKind::Notes: {
            std::istringstream words(value);
            std::string word;
            int pitch;
            while (words >> word) if (!parseNoteName(word, pitch)) return "'" + word + "' isn't a note: write them like E4, F#3, Bb2";
            return "";
        }
        case FieldKind::Places: {
            std::istringstream words(value);
            std::string word;
            while (words >> word){
                int string = 0, fret = 0;
                char rest[64] = "";
                if (std::sscanf(word.c_str(), "%d:%d%63s", &string, &fret, rest) < 2 || string < 1 || string > 12 || fret < 0 || fret > 24
                    || (rest[0] && rest[0] != ':'))
                    return "'" + word + "' isn't a place: string:fret, 1 = the lowest string (6:1), a label after another colon if wanted";
            }
            return "";
        }
        case FieldKind::Span: {
            std::istringstream numbers(value);
            int low = 0, high = 0;
            if (!(numbers >> low >> high) || !(numbers >> std::ws).eof() || low < field.min || high > field.max || low > high)
                return std::string("'") + field.key + "' is two numbers from " + std::to_string(field.min) + " to " + std::to_string(field.max)
                       + ", the lower first";
            return "";
        }
        case FieldKind::Key: {
            std::istringstream words(value);
            std::string tonic, mode;
            KeySignature key;
            if (!(words >> tonic >> mode) || !parseKeySignature(tonic, mode, key)) return "'" + value + "' isn't a key: write it like G major, E minor";
            return "";
        }
        case FieldKind::Page:
            return "";
        case FieldKind::Song:
            if (value.find_first_of("/\\") != std::string::npos || value == "." || value == "..") return "'" + value + "' isn't a song's folder name";
            return "";
        case FieldKind::File: {
            // Only a plain name: a shared lesson must not reach outside its own folder
            if (value.find_first_of("/\\") != std::string::npos || value == "." || value == "..")
                return "'" + value + "' must be a file in the lesson's folder";
            if (std::find(field.choices.begin(), field.choices.end(), lowercaseExtension(value)) == field.choices.end()){
                std::string list;
                for (const std::string& extension : field.choices) list += (list.empty() ? "" : ", ") + extension;
                return "'" + value + "' isn't a file it takes (" + list + ")";
            }
            return "";
        }
        default:
            return "";
    }
}

bool parseLessonDoc(const std::string& text, const std::string& path, LessonDoc& out, std::string& error, bool draft){
    const int version = versionOf(text);
    if (version == 0){ error = path + ": missing 'version'"; return false; }
    if (version < 0){ error = path + ": expected: version <number>"; return false; }
    if (version > LESSON_DOC_VERSION){
        error = path + ": lesson format v" + std::to_string(version) + " is newer than this build supports (v" + std::to_string(LESSON_DOC_VERSION) + ")";
        return false;
    }
    if (version == 1){ // steps: a page each
        Lesson lesson;
        if (!parseLesson(text, path, lesson, error)) return false;
        out = lessonDocFromSteps(lesson);
        return true;
    }

    out = LessonDoc{};
    int lineNumber = 0;
    auto lineError = [&](const std::string& message){
        error = path + ":" + std::to_string(lineNumber) + ": " + message;
        return false;
    };
    LessonPage* page = nullptr;
    LessonSection* section = nullptr;
    int column = 0;
    LessonBlock* block = nullptr;
    int blockLine = 0;
    std::string exerciseSource; // an exercise written in place, as it's read: blank lines where the block's own settings were
    int exerciseLine = 0;       //   (so its errors give the file's line numbers), from this line

    // A block read to its end: what it needs, and an exercise written in place read as one
    auto finishBlock = [&]() -> bool {
        if (!block) return true;
        const int at = lineNumber;
        lineNumber = blockLine;
        const BlockInfo& info = blockInfo(block->type);
        for (const BlockField& field : info.fields)
            if (field.required && !draft && blockValues(*block, field.key).empty()) return lineError(std::string("a ") + info.id + " block needs '" + field.key + "'");
        if (block->type == BlockType::Play && !draft){
            const bool song = !blockValues(*block, "song").empty(), file = !blockValues(*block, "file").empty();
            if (song && file) return lineError("a play block plays a song of the game's or a file, not both");
            if (!song && !file) return lineError("a play block needs a song ('song <its folder>') or a file ('file <name>.chart')");
        }
        if (block->type == BlockType::Exercise){
            const bool named = !blockValues(*block, "exercise").empty();
            if (named && !block->exerciseLines.empty())
                return lineError("an exercise block names an exercise or has one written in it, not both");
            if (!named && block->exerciseLines.empty() && !draft)
                return lineError("an exercise block needs an exercise: 'exercise <file name>', or its settings written in it");
            if (!named && !block->exerciseLines.empty()){
                std::string exerciseError;
                if (!parseExercise(exerciseSource, path, exerciseLine, block->name.empty() ? "Exercise" : block->name, block->exercise, exerciseError)){
                    error = exerciseError;
                    return false;
                }
                if (block->exercise.type == ExerciseType::Routine) return lineError("a routine can't be a block (it has no goal to reach)");
            }
        }
        block = nullptr;
        lineNumber = at;
        return true;
    };

    std::istringstream lines(text);
    std::string line;
    while (std::getline(lines, line)){
        lineNumber++;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        std::istringstream ss(line);
        std::string key;
        if (!(ss >> key) || key[0] == '#'){
            if (block && !block->exerciseLines.empty()) exerciseSource += "\n";
            continue;
        }
        std::string rest;
        std::getline(ss >> std::ws, rest);
        while (!rest.empty() && std::isspace((unsigned char)rest.back())) rest.pop_back();

        if (key == "version") continue;
        if (key == "page"){
            if (!finishBlock()) return false;
            out.pages.push_back({ rest, false, {} });
            page = &out.pages.back();
            section = nullptr;
            continue;
        }
        if (key == "section"){
            if (!finishBlock()) return false;
            if (!page) return lineError("'section' before the first page");
            const LayoutInfo* found = nullptr;
            for (const LayoutInfo& info : layouts()) if (rest == info.id || (rest.empty() && info.layout == SectionLayout::Single)) found = &info;
            if (!found) return lineError("unknown layout '" + rest + "' (known: single, halves, wide-narrow, narrow-wide, thirds)");
            page->sections.push_back(makeSection(found->layout));
            section = &page->sections.back();
            column = 0;
            continue;
        }
        if (key == "column"){
            if (!finishBlock()) return false;
            if (!section) return lineError("'column' outside a section");
            if (column + 1 >= (int)section->columns.size())
                return lineError(std::string("a ") + sectionLayoutId(section->layout) + " section has " + std::to_string(section->columns.size())
                                 + (section->columns.size() == 1 ? " column" : " columns"));
            column++;
            continue;
        }
        if (key == "block"){
            if (!finishBlock()) return false;
            if (!page) return lineError("'block' before the first page");
            if (!section){ // blocks straight on a page: one column
                page->sections.push_back(makeSection(SectionLayout::Single));
                section = &page->sections.back();
                column = 0;
            }
            std::istringstream words(rest);
            std::string id, name;
            words >> id;
            std::getline(words >> std::ws, name);
            const BlockInfo* info = findBlockInfo(id);
            if (!info){
                std::string known;
                for (const BlockInfo& each : blockInfos()) known += (known.empty() ? "" : ", ") + std::string(each.id);
                return lineError("unknown block '" + id + "' (known: " + known + ")");
            }
            section->columns[(size_t)column].push_back({});
            block = &section->columns[(size_t)column].back();
            block->type = info->type;
            block->name = name;
            blockLine = lineNumber;
            exerciseSource.clear();
            exerciseLine = 0;
            continue;
        }
        if (!page){ // the header
            if (key == "title") out.title = rest;
            else if (key == "category"){ if (!rest.empty()) out.category = rest; }
            else if (key == "author") out.author = rest;
            else if (key == "description") out.description = rest;
            else if (key == "instrument"){
                if (rest == "guitar") out.instrument = ExerciseInstrument::Guitar;
                else if (rest == "bass") out.instrument = ExerciseInstrument::Bass;
                else if (rest == "piano") out.instrument = ExerciseInstrument::Piano;
                else if (rest == "any") out.instrument = ExerciseInstrument::Any;
                else return lineError("instrument must be guitar, bass, piano or any");
            }
            else return lineError("unknown setting '" + key + "' before the first page");
            continue;
        }
        if (!block && !section && key == "aside"){ // the page's own setting, before its sections
            if (rest != "yes" && rest != "no") return lineError("'aside' is yes or no");
            page->aside = rest == "yes";
            continue;
        }
        if (!block) return lineError("'" + key + "' outside a block (a page holds sections, columns and blocks)");
        const BlockField* field = findBlockField(block->type, key);
        if (!field){
            if (block->type == BlockType::Exercise){ // an exercise's own setting: written in place
                if (block->exerciseLines.empty()) exerciseLine = lineNumber;
                block->exerciseLines.push_back(key + (rest.empty() ? "" : " " + rest));
                exerciseSource += key + (rest.empty() ? "" : " " + rest) + "\n";
                continue;
            }
            std::string known;
            for (const BlockField& each : blockInfo(block->type).fields) known += (known.empty() ? "" : ", ") + std::string(each.key);
            return lineError("'" + key + "' doesn't belong in a " + blockInfo(block->type).id + " block (it takes: " + known + ")");
        }
        const std::string problem = checkValue(*field, rest);
        if (!problem.empty()) return lineError(problem);
        if (field->kind != FieldKind::Paragraphs && !blockValues(*block, key).empty()) return lineError("'" + key + "' twice in one block");
        block->values.push_back({ key, rest });
        if (!block->exerciseLines.empty()) exerciseSource += "\n"; // its place kept in the exercise's line numbers
    }
    if (!finishBlock()) return false;
    if (out.title.empty()){ error = path + ": missing 'title'"; return false; }
    if (out.pages.empty()){ error = path + ": a lesson needs at least one page"; return false; }
    // The pages blocks send the student to are there
    if (!draft){
        for (const BlockPlace& place : lessonBlocks(out)){
            for (const char* key : { "help", "ace" }){
                const std::string title = blockValue(blockAt(out, place), key);
                if (!title.empty() && findPage(out, title) < 0){
                    error = path + ": page " + std::to_string(place.page + 1) + ": there's no page called '" + title + "' to send the student to";
                    return false;
                }
            }
        }
    }
    return true;
}

// --- Writing ------------------------------------------------------------------------------------------------------

// One value per line: a line break inside one would start a new, unintended line in the file
static std::string oneLine(const std::string& value){
    std::string line = value;
    std::replace(line.begin(), line.end(), '\n', ' ');
    std::replace(line.begin(), line.end(), '\r', ' ');
    return line;
}

std::string writeLessonDoc(const LessonDoc& doc){
    std::ostringstream out;
    out << "# lahn lesson\n";
    out << "version " << LESSON_DOC_VERSION << "\n";
    out << "title " << oneLine(doc.title) << "\n";
    out << "category " << oneLine(doc.category) << "\n";
    if (!doc.author.empty()) out << "author " << oneLine(doc.author) << "\n";
    if (!doc.description.empty()) out << "description " << oneLine(doc.description) << "\n";
    if (doc.instrument != ExerciseInstrument::Any) out << "instrument " << instrumentId(doc.instrument) << "\n";
    out << writeLessonPages(doc);
    return out.str();
}

std::string writeLessonPages(const LessonDoc& doc){
    std::ostringstream out;
    for (const LessonPage& page : doc.pages){
        out << "\npage" << (page.title.empty() ? "" : " " + oneLine(page.title)) << "\n";
        if (page.aside) out << "  aside yes\n";
        for (const LessonSection& section : page.sections){
            out << "  section " << sectionLayoutId(section.layout) << "\n";
            for (size_t c = 0; c < section.columns.size(); c++){
                if (c > 0) out << "  column\n";
                for (const LessonBlock& block : section.columns[c]){
                    out << "    block " << blockInfo(block.type).id << (block.name.empty() ? "" : " " + oneLine(block.name)) << "\n";
                    for (const auto& [key, value] : block.values){
                        // A paragraph typed with line breaks is written as several paragraphs, one a line
                        if (findBlockField(block.type, key) && findBlockField(block.type, key)->kind == FieldKind::Paragraphs){
                            std::istringstream paragraphs(value);
                            std::string paragraph;
                            while (std::getline(paragraphs, paragraph)) if (!paragraph.empty()) out << "      " << key << " " << paragraph << "\n";
                        } else if (!value.empty()) out << "      " << key << " " << oneLine(value) << "\n";
                    }
                    for (const std::string& setting : block.exerciseLines) out << "      " << oneLine(setting) << "\n";
                }
            }
        }
    }
    return out.str();
}

LessonDoc lessonDocFromSteps(const Lesson& lesson){
    LessonDoc doc;
    doc.title = lesson.title;
    doc.category = lesson.category;
    doc.author = lesson.author;
    doc.description = lesson.description;
    for (const LessonStep& step : lesson.steps){
        LessonPage page;
        page.title = step.title;
        page.sections.push_back(makeSection(SectionLayout::Single));
        LessonBlock block;
        switch (step.type){
            case LessonStepType::Text: block.type = BlockType::Text; break;
            case LessonStepType::Image: block.type = BlockType::Image; break;
            case LessonStepType::Audio: block.type = BlockType::Audio; break;
            case LessonStepType::Video: block.type = BlockType::Video; break;
            case LessonStepType::Exercise: block.type = BlockType::Exercise; break;
            case LessonStepType::Play: block.type = BlockType::Play; break;
        }
        for (const std::string& paragraph : step.paragraphs) block.values.push_back({ "text", paragraph });
        if (!step.file.empty()) block.values.push_back({ "file", step.file });
        if (!step.caption.empty()) block.values.push_back({ "caption", step.caption });
        if (!step.exercise.empty()) block.values.push_back({ "exercise", step.exercise });
        if (step.inlined) block.exercise = step.inlineExercise; // (a course's: read, not written back, until courses are lessons)
        if (step.goal > 0) block.values.push_back({ "goal", std::to_string(step.goal) });
        // A text step with a title only: its page says it
        if (block.type != BlockType::Text || !step.paragraphs.empty()) page.sections[0].columns[0].push_back(block);
        doc.pages.push_back(page);
    }
    return doc;
}

// --- Files --------------------------------------------------------------------------------------------------------

bool checkLessonFiles(const LessonDoc& doc, const std::string& folder, std::string& error){
    for (const BlockPlace& place : lessonBlocks(doc)){
        const LessonBlock& block = blockAt(doc, place);
        for (const BlockField& field : blockInfo(block.type).fields){
            if (field.kind != FieldKind::File) continue;
            for (const std::string& file : blockValues(block, field.key)){
                const fs::path media = fs::path(folder) / file;
                const std::string where = "page " + std::to_string(place.page + 1) + ", " + blockInfo(block.type).id + " block: ";
                if (!fs::is_regular_file(media)){
                    error = where + "'" + file + "' isn't in the lesson's folder";
                    return false;
                }
                if (block.type == BlockType::Play){
                    Chart chart;
                    std::string chartError;
                    if (!loadChart(media.string(), chart, chartError)){
                        error = where + chartError;
                        return false;
                    }
                }
            }
        }
    }
    return true;
}

bool loadLessonDoc(const std::string& folder, LessonDoc& out, std::string& error){
    const std::string path = (fs::path(folder) / LESSON_FILE_NAME).string();
    std::ifstream file(path);
    if (!file){
        error = path + ": could not open file";
        return false;
    }
    std::stringstream text;
    text << file.rdbuf();
    if (!parseLessonDoc(text.str(), path, out, error)) return false;
    if (!checkLessonFiles(out, folder, error)){
        error = path + ": " + error;
        return false;
    }
    return true;
}

bool loadLessonDraft(const std::string& folder, LessonDoc& out, std::string& error){
    const std::string path = (fs::path(folder) / LESSON_FILE_NAME).string();
    std::ifstream file(path);
    if (!file){
        error = path + ": could not open file";
        return false;
    }
    std::stringstream text;
    text << file.rdbuf();
    return parseLessonDoc(text.str(), path, out, error, true);
}

bool saveLessonDoc(const std::string& folder, const LessonDoc& doc, std::string& error){
    std::error_code ec;
    fs::create_directories(folder, ec);
    return writeFileAtomically((fs::path(folder) / LESSON_FILE_NAME).string(), writeLessonDoc(doc), error);
}

// --- Making lessons -------------------------------------------------------------------------------------------------

std::string checkBlockValue(const BlockField& field, const std::string& value){
    return checkValue(field, value);
}

LessonBlock makeBlock(BlockType type){
    LessonBlock block;
    block.type = type;
    switch (type){
        case BlockType::Text: block.values = { { "text", "Write here. **Bold** words, and notes to click and hear: [E4] [F4]." } }; break;
        case BlockType::Heading: block.values = { { "text", "A heading" } }; break;
        case BlockType::Callout: block.values = { { "style", "tip" }, { "text", "A tip for the student." } }; break;
        case BlockType::Reveal: block.values = { { "text", "The answer, shown once it's asked for." } }; break;
        case BlockType::Fretboard: block.values = { { "frets", "0 5" }, { "lit", "6:1" } }; break;
        case BlockType::Keyboard: block.values = { { "from", "C4" }, { "to", "B4" }, { "lit", "C4 E4 G4" } }; break;
        case BlockType::Staff: block.values = { { "notes", "E4 F4 G4" } }; break;
        default: break; // a file or an exercise to choose: nothing yet
    }
    return block;
}

bool setBlockExercise(LessonBlock& block, const std::string& lines, std::string& error){
    std::vector<std::string> kept;
    std::istringstream text(lines);
    std::string line;
    while (std::getline(text, line)){
        while (!line.empty() && (line.back() == '\r' || std::isspace((unsigned char)line.back()))) line.pop_back();
        const size_t start = line.find_first_not_of(" \t");
        if (start == std::string::npos || line[start] == '#') continue;
        line = line.substr(start);
        // The block's own settings don't belong among the exercise's
        std::istringstream words(line);
        std::string key;
        words >> key;
        if (findBlockField(block.type, key)){
            error = "'" + key + "' is the block's own setting, not the exercise's";
            return false;
        }
        kept.push_back(line);
    }
    if (kept.empty()){
        error = "an exercise needs its settings: its type first (type notes, type reading...)";
        return false;
    }
    std::string source;
    for (const std::string& setting : kept) source += setting + "\n";
    ExerciseFile exercise;
    if (!parseExercise(source, "the exercise", 1, block.name.empty() ? "Exercise" : block.name, exercise, error)) return false;
    if (exercise.type == ExerciseType::Routine){
        error = "a routine can't be a block (it has no goal to reach)";
        return false;
    }
    block.exerciseLines = kept;
    block.exercise = exercise;
    setBlockValue(block, "exercise", ""); // written here, not named
    return true;
}

// The settings each kind of exercise's form shows (the rest stay as written, in its text)
static BlockField setting(const char* key, const char* name, FieldKind kind, const char* description, const char* standard = "",
                          std::vector<std::string> choices = {}){
    return { key, name, kind, description, standard, std::move(choices) };
}

const std::vector<ExerciseForm>& exerciseForms(){
    static const BlockField tempo = setting("tempo", "Tempo", FieldKind::Text, "From, to and the step it goes up by, in bpm: 90 120 5");
    static const BlockField challenge = setting("challenge", "To pass", FieldKind::Text, "A clean pass at this tempo or faster passes it (left out: its first tempo)");
    static const BlockField pass = setting("pass", "Clean at", FieldKind::Text, "The share of notes right (percent) for a pass to be clean", "90");
    static const BlockField cells = setting("cells", "Rhythms", FieldKind::Text,
        "The beats it's made of: quarter, eighths, rest, offbeat, triplets, sixteenths, dotted, gallop, reverse_gallop", "quarter eighths rest");
    static const BlockField bars = setting("bars", "Bars", FieldKind::Text, "How many bars a pass (1 to 16)", "2");
    static const BlockField instrument = setting("instrument", "Played on", FieldKind::Choice, "The instrument it's played on", "guitar",
                                                 { "guitar", "bass", "piano" });
    static const std::vector<ExerciseForm> forms = {
        { "notes", "Play the notes", "Notes asked one at a time, no clock: where they are, their names, the staff, or by ear",
          "type notes\nnotes E4 F4 G4\nshow staff", {
            setting("notes", "Notes", FieldKind::Text, "The notes asked: E4 F4 G4"),
            setting("show", "Asked as", FieldKind::Choice, "Where to play it (the neck, or the keys on a piano), its name, written on the staff, or heard",
                    "neck", { "neck", "keys", "name", "staff", "ear" }),
            setting("where", "Shown where too", FieldKind::Toggle, "Its place lit as well (for a name, the staff, or by ear)", "no"),
            setting("count", "Notes a run", FieldKind::Text, "How many are asked in a run", "8"),
            setting("pass", "To pass a run", FieldKind::Text, "How many right the first time", "7"),
            setting("order", "Order", FieldKind::Choice, "At random, or as written, going round", "random", { "random", "in_order" }),
            setting("octave", "Octave", FieldKind::Choice, "The octave written, or any (the name's enough)", "exact", { "exact", "any" }),
            setting("reference", "Heard first", FieldKind::Text, "By ear: a note played before each, to hear it against (E4)"),
            setting("key", "Key", FieldKind::Text, "The staff's key signature: C major, G major, E minor"),
            instrument,
        } },
        { "reading", "Read to a beat", "Notes read off the staff and played in time, a new pass each time, faster as it goes",
          "type reading\nnotes E4 F4 G4\ncells quarter\nbars 10\ntempo 90 120 5\nchallenge 100\npass 87", {
            setting("notes", "Notes", FieldKind::Text, "Just these notes, at random (E4 F4 G4); or leave it out for melodies in a key"),
            setting("key", "Key", FieldKind::Text, "Melodies in this key: its note (C, G, F#)", "C"),
            setting("scale", "Scale", FieldKind::Text, "major, minor, and the others in the game", "major"),
            setting("frets", "Frets", FieldKind::Text, "Melodies in these frets: 0 3"),
            setting("range", "Range", FieldKind::Text, "On a piano: melodies from one note to another (C4 G4)"),
            setting("where", "Shown where", FieldKind::Toggle, "The neck (or keys) shown too, the next note lit", "no"),
            cells, bars, tempo, challenge, pass, instrument,
        } },
        { "scale", "Scale drill", "A scale up and down to a beat, faster each clean pass", "type scale\nkey G\nscale major\noctaves 1", {
            setting("key", "Key", FieldKind::Text, "Its note: C, G, F#, Bb", "G"),
            setting("scale", "Scale", FieldKind::Text, "major, minor, and the others in the game", "major"),
            setting("octaves", "Octaves", FieldKind::Text, "1 to 3", "2"),
            setting("fingering", "Fingering", FieldKind::Choice, "In one position, or three notes a string", "position", { "position", "3nps" }),
            setting("direction", "Direction", FieldKind::Choice, "Up, down, or up and back down", "up_down", { "up", "down", "up_down" }),
            setting("notes_per_beat", "Notes a beat", FieldKind::Text, "1 (quarters) to 4 (sixteenths)", "2"),
            tempo, challenge, pass,
        } },
        { "rhythm", "Rhythm drill", "Rhythms read and played on a beat: only the timing counts", "type rhythm\ncells quarter eighths rest\nbars 2", {
            cells, bars,
            setting("time", "Beats a bar", FieldKind::Text, "2 to 7 (x/4)", "4"),
            tempo, pass,
        } },
        { "chords", "Chord changes", "Chords played in turn, to a beat", "type chords\nchords Em C G D", {
            setting("chords", "Chords", FieldKind::Text, "Their names, in turn: Em C G D"),
            setting("beats", "Beats each", FieldKind::Text, "1 to 8", "4"),
            setting("rounds", "Rounds", FieldKind::Text, "Times through them in a pass (1 to 8)", "2"),
            tempo, pass,
        } },
        { "intervals", "Intervals by ear", "Two notes: how far apart?", "type intervals\ndirection up\nintervals 7 4\nstart 2", {
            setting("direction", "Played", FieldKind::Choice, "One after the other going up, going down, or together", "up", { "up", "down", "together" }),
            setting("intervals", "Intervals", FieldKind::Text, "In semitones, in the order they come in: 7 4 12"),
            setting("start", "To begin with", FieldKind::Text, "How many of them at first"),
        } },
        { "singing", "Sing it back", "A note played, sung back in tune", "type singing\nrange 48 67", {
            setting("range", "Range", FieldKind::Text, "The lowest and highest note asked (MIDI numbers: 48 67)", "48 67"),
            setting("notes", "Notes", FieldKind::Choice, "The white keys only, or every note", "naturals", { "naturals", "all" }),
            setting("octave", "Octave", FieldKind::Choice, "Any octave counts, or the one played", "any", { "any", "exact" }),
            setting("tolerance", "In tune within", FieldKind::Text, "Cents off that still count (5 to 50)", "30"),
        } },
    };
    return forms;
}

const ExerciseForm* findExerciseForm(const std::string& type){
    for (const ExerciseForm& form : exerciseForms()) if (type == form.type) return &form;
    return nullptr;
}

std::string exerciseSetting(const LessonBlock& block, const std::string& key){
    for (const std::string& line : block.exerciseLines){
        std::istringstream words(line);
        std::string first, rest;
        words >> first;
        if (first != key) continue;
        std::getline(words >> std::ws, rest);
        return rest;
    }
    return "";
}

bool setExerciseSetting(LessonBlock& block, const std::string& key, const std::string& value, std::string& error){
    std::string lines;
    bool found = false;
    for (const std::string& line : block.exerciseLines){
        std::istringstream words(line);
        std::string first;
        words >> first;
        if (first == key){
            if (!found && !value.empty()) lines += key + " " + value + "\n"; // in its place
            found = true;
        } else lines += line + "\n";
    }
    if (!found && !value.empty()) lines += key + " " + value + "\n";
    LessonBlock changed = block;
    if (!setBlockExercise(changed, lines, error)) return false;
    block = changed;
    return true;
}

LessonBlock* blockPointer(LessonDoc& doc, const BlockPlace& place){
    if (place.page < 0 || place.page >= (int)doc.pages.size()) return nullptr;
    LessonPage& page = doc.pages[(size_t)place.page];
    if (place.section < 0 || place.section >= (int)page.sections.size()) return nullptr;
    LessonSection& section = page.sections[(size_t)place.section];
    if (place.column < 0 || place.column >= (int)section.columns.size()) return nullptr;
    std::vector<LessonBlock>& column = section.columns[(size_t)place.column];
    if (place.block < 0 || place.block >= (int)column.size()) return nullptr;
    return &column[(size_t)place.block];
}

// The column a place is in, or nullptr
static std::vector<LessonBlock>* columnAt(LessonDoc& doc, const BlockPlace& place){
    if (place.page < 0 || place.page >= (int)doc.pages.size()) return nullptr;
    LessonPage& page = doc.pages[(size_t)place.page];
    if (place.section < 0 || place.section >= (int)page.sections.size()) return nullptr;
    LessonSection& section = page.sections[(size_t)place.section];
    if (place.column < 0 || place.column >= (int)section.columns.size()) return nullptr;
    return &section.columns[(size_t)place.column];
}

bool insertBlock(LessonDoc& doc, const BlockPlace& at, const LessonBlock& block){
    std::vector<LessonBlock>* column = columnAt(doc, at);
    if (!column || at.block < 0 || at.block > (int)column->size()) return false;
    column->insert(column->begin() + at.block, block);
    return true;
}

bool removeBlock(LessonDoc& doc, const BlockPlace& place){
    std::vector<LessonBlock>* column = columnAt(doc, place);
    if (!column || place.block < 0 || place.block >= (int)column->size()) return false;
    column->erase(column->begin() + place.block);
    return true;
}

BlockPlace moveBlock(LessonDoc& doc, const BlockPlace& from, const BlockPlace& to){
    const LessonBlock* moving = blockPointer(doc, from);
    std::vector<LessonBlock>* target = columnAt(doc, to);
    if (!moving || !target || to.block < 0 || to.block > (int)target->size()) return from;
    const LessonBlock block = *moving;
    BlockPlace landed = to;
    // In its own column, taking it out moves up what was after it
    const bool sameColumn = from.page == to.page && from.section == to.section && from.column == to.column;
    if (sameColumn && to.block > from.block) landed.block--;
    if (sameColumn && landed.block == from.block) return from; // where it already is
    removeBlock(doc, from);
    insertBlock(doc, landed, block);
    return landed;
}

// A neck's places as a setting writes them: string:fret (1 = the lowest string), and its label after another colon
static std::string writeNeckPlaces(const std::vector<NeckPlace>& places){
    std::string text;
    for (const NeckPlace& place : places)
        text += (text.empty() ? "" : " ") + std::to_string(place.string + 1) + ":" + std::to_string(place.fret) + (place.label.empty() ? "" : ":" + place.label);
    return text;
}

void cycleNeckPlace(LessonBlock& block, int string, int fret){
    std::vector<NeckPlace> dots = readNeckPlaces(blockValue(block, "dots")), lit = readNeckPlaces(blockValue(block, "lit"));
    auto at = [&](std::vector<NeckPlace>& places){
        return std::find_if(places.begin(), places.end(), [&](const NeckPlace& place){ return place.string == string && place.fret == fret; });
    };
    if (auto found = at(lit); found != lit.end()) lit.erase(found);         // lit: gone
    else if (auto dot = at(dots); dot != dots.end()){                        // a note: lit
        lit.push_back(*dot);
        dots.erase(dot);
    } else dots.push_back({ string, fret, "" });                             // nothing: a note
    setBlockValue(block, "dots", writeNeckPlaces(dots));
    setBlockValue(block, "lit", writeNeckPlaces(lit));
}

void toggleLitKey(LessonBlock& block, int pitch){
    std::vector<int> lit = readNotes(blockValue(block, "lit"));
    auto found = std::find(lit.begin(), lit.end(), pitch);
    if (found != lit.end()) lit.erase(found);
    else lit.push_back(pitch);
    std::sort(lit.begin(), lit.end());
    std::string text;
    for (int key : lit) text += std::string(text.empty() ? "" : " ") + pitchClassName(key) + std::to_string(pitchOctave(key));
    setBlockValue(block, "lit", text);
}

void setSectionLayout(LessonSection& section, SectionLayout layout){
    const size_t columns = (size_t)sectionColumns(layout);
    if (section.columns.size() > columns){
        std::vector<LessonBlock>& last = section.columns[columns - 1];
        for (size_t c = columns; c < section.columns.size(); c++) last.insert(last.end(), section.columns[c].begin(), section.columns[c].end());
    }
    section.columns.resize(columns);
    section.layout = layout;
}

// --- Templates -----------------------------------------------------------------------------------------------------

namespace {

// A note to start from on each instrument (a lesson's maker changes it): where it is, and its name
struct SampleNote {
    std::string name;    // "F4"
    std::string place;   // on the neck, "6:1"; a piano's: none
    std::string around;  // its neighbours, for the drills that mix: "E4 F4 G4"
};
SampleNote sampleNote(ExerciseInstrument instrument){
    switch (instrument){
        case ExerciseInstrument::Bass: return { "C3", "4:5", "A2 B2 C3" };
        case ExerciseInstrument::Piano: return { "D4", "", "C4 D4 E4" };
        default: return { "F4", "6:1", "E4 F4 G4" };
    }
}

LessonBlock textBlock(const std::vector<std::string>& paragraphs){
    LessonBlock block = makeBlock(BlockType::Text);
    setBlockValues(block, "text", paragraphs);
    return block;
}

LessonBlock calloutBlock(const char* style, const std::string& words){
    LessonBlock block = makeBlock(BlockType::Callout);
    setBlockValue(block, "style", style);
    setBlockValues(block, "text", { words });
    return block;
}

// Where the note is: on the neck, or on the keys
LessonBlock whereBlock(ExerciseInstrument instrument, const SampleNote& note){
    if (instrument == ExerciseInstrument::Piano){
        LessonBlock keys = makeBlock(BlockType::Keyboard);
        setBlockValue(keys, "from", "C4");
        setBlockValue(keys, "to", "B4");
        setBlockValue(keys, "lit", note.name);
        return keys;
    }
    LessonBlock neck = makeBlock(BlockType::Fretboard);
    setBlockValue(neck, "frets", "0 5");
    setBlockValue(neck, "lit", note.place);
    return neck;
}

// A drill made in the lesson, of a kind, with these notes (on the lesson's instrument)
LessonBlock drillBlock(const std::string& name, const char* type, const std::string& notes, ExerciseInstrument instrument, const char* extra = ""){
    LessonBlock block = makeBlock(BlockType::Exercise);
    block.name = name;
    std::string lines = std::string("type ") + type + "\nnotes " + notes + "\n" + extra;
    if (instrument == ExerciseInstrument::Piano) lines += "instrument piano\n";
    else if (instrument == ExerciseInstrument::Bass) lines += std::string(type) == "reading" ? "tuning 28 33 38 43\n" : "instrument bass\n";
    std::string error;
    setBlockExercise(block, lines, error);
    return block;
}

LessonPage pageOf(const std::string& title, std::vector<LessonSection> sections){
    LessonPage page;
    page.title = title;
    page.sections = std::move(sections);
    return page;
}

LessonSection single(std::vector<LessonBlock> blocks){
    LessonSection section = makeSection(SectionLayout::Single);
    section.columns[0] = std::move(blocks);
    return section;
}

LessonSection wideNarrow(std::vector<LessonBlock> wide, std::vector<LessonBlock> narrow){
    LessonSection section = makeSection(SectionLayout::WideNarrow);
    section.columns[0] = std::move(wide);
    section.columns[1] = std::move(narrow);
    return section;
}

} // namespace

const std::vector<LessonTemplate>& pageTemplates(){
    static const std::vector<LessonTemplate> templates = {
        { "blank", "Blank", "A title and a few words" },
        { "explain", "Explain", "Words beside the neck (or the keys), a tip under them" },
        { "drill", "A drill", "A few words, then a drill to pass" },
        { "song", "A song", "A few bars of one of the game's songs, slowly" },
        { "question", "A question", "A question, its answer hidden behind a button" },
        { "help", "Help", "Set aside: shown when a drill's missed twice, then back" },
    };
    return templates;
}

LessonPage makePage(const std::string& id, ExerciseInstrument instrument){
    const SampleNote note = sampleNote(instrument);
    if (id == "explain")
        return pageOf("A new page", { wideNarrow({ textBlock({ "What it is, in a few words. Notes can be clicked to hear them: [" + note.name + "]." }),
                                                   calloutBlock("tip", "Something that makes it easier.") },
                                                 { whereBlock(instrument, note) }) });
    if (id == "drill")
        return pageOf("Play it", { single({ textBlock({ "What to do, in a line." }), drillBlock("Play it", "notes", note.around, instrument, "show staff\n") }) });
    if (id == "song"){
        LessonBlock song = makeBlock(BlockType::Play);
        setBlockValue(song, "song", "first-light");
        setBlockValue(song, "bars", "1 4");
        setBlockValue(song, "tempo", "70");
        song.name = "The first four bars, slowly";
        return pageOf("Play along", { single({ textBlock({ "Which bars, and what to listen for." }), song }) });
    }
    if (id == "question"){
        LessonBlock reveal = makeBlock(BlockType::Reveal);
        setBlockValue(reveal, "label", "Show the answer");
        setBlockValues(reveal, "text", { "The answer: [" + note.name + "]." });
        return pageOf("A question", { single({ textBlock({ "Which note is this?" }), reveal }) });
    }
    if (id == "help"){
        LessonPage page = pageOf("Some help", { wideNarrow({ textBlock({ "The same thing again, slower, another way." }) },
                                                         { whereBlock(instrument, note) }) });
        page.aside = true;
        return page;
    }
    return pageOf("A new page", { single({ textBlock({ "Write here." }) }) });
}

const std::vector<LessonTemplate>& lessonTemplates(){
    static const std::vector<LessonTemplate> templates = {
        { "blank", "Blank", "One page to start from" },
        { "new-note", "A new note", "Meet it, play it, mix it with its neighbours; help when it's missed; practice to end" },
        { "riff", "Learn a riff", "A few bars of a song: listen, play them slowly, then at full speed" },
        { "ear", "Ear training", "Notes heard and played back, then intervals by name" },
    };
    return templates;
}

LessonDoc makeLesson(const std::string& id, const std::string& title, ExerciseInstrument instrument){
    LessonDoc doc;
    doc.title = title;
    doc.instrument = instrument;
    const SampleNote note = sampleNote(instrument);
    if (id == "new-note"){
        doc.description = "A new note: where it is, how it's written, and playing it.";
        LessonPage meet = makePage("explain", instrument);
        meet.title = "Meet " + note.name.substr(0, note.name.size() - 1);
        LessonBlock first = drillBlock("Play it", "notes", note.name, instrument, "show staff\ncount 4\npass 3\n");
        setBlockValue(first, "help", "Finding it");
        LessonPage play = pageOf("Play it", { single({ textBlock({ "It lights on the staff: play it." }), first }) });
        LessonPage mix = pageOf("With its neighbours", { single({ textBlock({ "Now among the notes around it, read to a beat." }),
                                                                  drillBlock("Read them to a beat", "reading", note.around, instrument,
                                                                             "cells quarter\nbars 8\ntempo 80 110 5\nchallenge 90\npass 87\n") }) });
        LessonPage help = makePage("help", instrument);
        help.title = "Finding it";
        LessonBlock practice = makeBlock(BlockType::Practice);
        setBlockValue(practice, "from", "lesson");
        practice.name = "Once more";
        LessonPage done = pageOf("Well done", { single({ calloutBlock("remember", "The one thing to remember about it."), practice }) });
        doc.pages = { meet, play, mix, help, done };
    } else if (id == "riff"){
        doc.description = "A riff from one of the game's songs, a little at a time.";
        LessonBlock slowly = makeBlock(BlockType::Play), full = makeBlock(BlockType::Play);
        for (LessonBlock* song : { &slowly, &full }){
            setBlockValue(*song, "song", "first-light");
            setBlockValue(*song, "bars", "1 4");
        }
        setBlockValue(slowly, "tempo", "70");
        slowly.name = "Slowly";
        full.name = "At full speed";
        doc.pages = {
            pageOf("Listen", { single({ textBlock({ "What the riff is, and what to listen for in it." }),
                                        calloutBlock("tip", "Choose the song and its bars in the song blocks on the next pages.") }) }),
            pageOf("Slowly", { single({ textBlock({ "Its first bars, at 70%." }), slowly }) }),
            pageOf("At full speed", { single({ textBlock({ "The same bars, as the song has them." }), full }) }),
        };
    } else if (id == "ear"){
        doc.description = "Notes heard, played back; then how far apart two notes are.";
        LessonBlock intervals = makeBlock(BlockType::Exercise);
        intervals.name = "Fifth or third?";
        std::string error;
        setBlockExercise(intervals, "type intervals\ndirection up\nintervals 7 4\nstart 2", error);
        doc.pages = {
            pageOf("Hear it, play it", { single({ textBlock({ "lahn plays a note: find it and play it back." }),
                                                  drillBlock("By ear", "notes", note.around, instrument, "show ear\nwhere yes\n") }) }),
            pageOf("How far apart?", { single({ textBlock({ "Two notes: a fifth, or a third?" }), intervals }) }),
        };
    } else doc.pages = { makePage("blank", instrument) };
    return doc;
}

// --- Lessons in folders --------------------------------------------------------------------------------------------

std::string practiceExercise(const LessonBlock& block, const std::vector<int>& pitches, ExerciseInstrument instrument){
    std::string names;
    for (int pitch : pitches) names += std::string(names.empty() ? "" : " ") + pitchClassName(pitch) + std::to_string(pitchOctave(pitch));
    const bool piano = instrument == ExerciseInstrument::Piano, bass = instrument == ExerciseInstrument::Bass;
    std::string lines;
    if (blockValue(block, "as") == "reading"){
        lines = "type reading\nnotes " + names + "\ncells quarter\nbars 8\ntempo 80 120 5\npass 87\n";
        if (piano) lines += "instrument piano\n";
        else if (bass) lines += "tuning 28 33 38 43\n";
    } else {
        lines = "type notes\nnotes " + names + "\nshow staff\ncount " + std::to_string(std::max(6, (int)pitches.size() * 2)) + "\n";
        if (piano) lines += "instrument piano\n";
        else if (bass) lines += "instrument bass\n";
    }
    return lines;
}

std::vector<int> lessonNotes(const LessonDoc& doc){
    std::set<int> found;
    for (const BlockPlace& place : lessonBlocks(doc)){
        const LessonBlock& block = blockAt(doc, place);
        std::vector<int> pitches;
        if (block.type == BlockType::Staff) pitches = readNotes(blockValue(block, "notes"));
        else if (block.type == BlockType::Keyboard) pitches = readNotes(blockValue(block, "lit"));
        else if (block.type == BlockType::Exercise){
            const std::string type = exerciseSetting(block, "type");
            if (type == "notes" || type == "reading") pitches = readNotes(exerciseSetting(block, "notes"));
        }
        found.insert(pitches.begin(), pitches.end());
    }
    return std::vector<int>(found.begin(), found.end());
}

std::string songBlockChart(const LessonBlock& block, const std::string& lessonFolder, const std::vector<std::string>& songFolders){
    const std::string song = blockValue(block, "song"), file = blockValue(block, "file");
    if (!song.empty()){
        for (const std::string& folder : songFolders){
            const fs::path chart = fs::path(folder) / song / "song.chart";
            if (fs::exists(chart)) return chart.string();
        }
        return "";
    }
    return file.empty() ? "" : (fs::path(lessonFolder) / file).string();
}

void checkLessonSongs(std::vector<LessonEntry>& lessons, const std::vector<std::string>& songFolders){
    for (LessonEntry& entry : lessons){
        if (!entry.error.empty()) continue;
        for (const BlockPlace& place : lessonBlocks(entry.doc)){
            const LessonBlock& block = blockAt(entry.doc, place);
            const std::string song = blockValue(block, "song");
            if (block.type != BlockType::Play || song.empty() || !songBlockChart(block, entry.folder, songFolders).empty()) continue;
            entry.error = fs::path(entry.folder).filename().string() + "/" + LESSON_FILE_NAME + ": page " + std::to_string(place.page + 1)
                        + ": there's no song called " + song;
            break;
        }
    }
}

std::vector<LessonEntry> scanLessons(const std::string& dir, bool builtIn){
    std::vector<LessonEntry> lessons;
    std::error_code ec; // a missing folder just means no lessons
    for (const fs::directory_entry& folder : fs::directory_iterator(dir, ec)){
        if (!folder.is_directory() || !fs::exists(folder.path() / LESSON_FILE_NAME)) continue;
        LessonEntry entry;
        entry.folder = folder.path().string();
        entry.builtIn = builtIn;
        const std::string name = folder.path().filename().string();
        entry.id = std::string(builtIn ? "builtin-" : "user-") + name;
        const std::string path = (folder.path() / LESSON_FILE_NAME).string();
        std::ifstream file(path);
        std::stringstream text;
        text << file.rdbuf();
        entry.version = versionOf(text.str());
        if (!loadLessonDoc(entry.folder, entry.doc, entry.error)){
            entry.lesson.title = name;
            // Menus show the error: "folder/lesson.lesson" is enough there, the full path would take several lines
            if (entry.error.rfind(path, 0) == 0) entry.error = name + "/" + LESSON_FILE_NAME + entry.error.substr(path.size());
        } else {
            entry.lesson.title = entry.doc.title;
            entry.lesson.category = entry.doc.category;
            entry.lesson.author = entry.doc.author;
            entry.lesson.description = entry.doc.description;
            std::string stepsError;
            if (entry.version == 1) loadLesson(entry.folder, entry.lesson, stepsError);
        }
        lessons.push_back(entry);
    }
    std::sort(lessons.begin(), lessons.end(), [](const LessonEntry& a, const LessonEntry& b){
        if (a.lesson.category != b.lesson.category) return a.lesson.category < b.lesson.category;
        return a.lesson.title < b.lesson.title;
    });
    return lessons;
}

void checkLessonExercises(std::vector<LessonEntry>& lessons, const std::vector<ExerciseEntry>& exercises){
    for (LessonEntry& entry : lessons){
        if (!entry.error.empty()) continue;
        const std::string where = fs::path(entry.folder).filename().string() + "/" + LESSON_FILE_NAME + ": page ";
        for (const BlockPlace& place : lessonBlocks(entry.doc)){
            const LessonBlock& block = blockAt(entry.doc, place);
            const std::string name = blockValue(block, "exercise");
            if (block.type != BlockType::Exercise || name.empty()) continue;
            const ExerciseEntry* found = findExercise(exercises, entry.builtIn, name);
            const std::string blockName = where + std::to_string(place.page + 1) + " (" + name + ")";
            if (!found) entry.error = blockName + ": there's no " + name + ".exercise";
            else if (!found->error.empty()) entry.error = blockName + ": that exercise has an error of its own";
            else if (found->exercise.type == ExerciseType::Routine) entry.error = blockName + ": a routine can't be a lesson's exercise";
            if (!entry.error.empty()) break;
        }
    }
}

const int BLOCKS_A_PAGE = 1000; // a scored block's key: its page's number times this, and which of the page's it is

std::vector<BlockPlace> scoredBlocks(const LessonDoc& doc, int page){
    std::vector<BlockPlace> found;
    for (const BlockPlace& place : lessonBlocks(doc)) if (place.page == page && blockScored(blockAt(doc, place))) found.push_back(place);
    return found;
}

int scoredBlockKey(const LessonDoc& doc, const BlockPlace& place){
    const std::vector<BlockPlace> onPage = scoredBlocks(doc, place.page);
    int which = 0;
    for (int i = 0; i < (int)onPage.size(); i++){
        const BlockPlace& other = onPage[(size_t)i];
        if (other.section == place.section && other.column == place.column && other.block == place.block) which = i;
    }
    return place.page * BLOCKS_A_PAGE + which;
}

void upgradeLessonProgress(LessonProgress& progress, int version){
    if (version != 1) return;
    std::vector<int> passed;
    for (int step : progress.passed) passed.push_back(step < BLOCKS_A_PAGE ? step * BLOCKS_A_PAGE : step); // a step was a page
    progress.passed.clear();
    for (int key : passed) passStep(progress, key);
}

// As a version 1 step, to ask lessonGoal and its text what it takes
static LessonStep asStep(const LessonBlock& block){
    LessonStep step;
    step.type = block.type == BlockType::Play ? LessonStepType::Play : LessonStepType::Exercise;
    step.goal = blockGoal(block);
    return step;
}

int scoredBlockGoal(const LessonBlock& block, ExerciseType exerciseType){
    return lessonGoal(asStep(block), exerciseType);
}

std::string scoredBlockGoalText(const LessonBlock& block, const ExerciseEntry* exercise){
    return lessonGoalText(asStep(block), exercise);
}
