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
#include <sstream>

namespace fs = std::filesystem;

// --- What each block takes ----------------------------------------------------------------------------------------

static const std::vector<std::string> IMAGE_FILES = { ".png", ".jpg", ".jpeg" };
static const std::vector<std::string> SOUND_FILES = { ".wav", ".flac", ".mp3" };
static const std::vector<std::string> VIDEO_FILES = { ".mpg", ".mpeg" }; // MPEG-1: see the video player
static const std::vector<std::string> CHART_FILES = { ".chart" };

// What every scored block takes
static BlockField gateField(){
    return { "gate", "Must pass", FieldKind::Toggle, "Passed before going on; or not, and it's there to practise", "yes" };
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
            { "exercise", "Exercise", FieldKind::Exercise, "One of the game's exercises, by its file name; or leave it out and write one here" },
            goalField("Clean passes for a drill, right answers in a row for intervals (left out: the usual)", 999),
            gateField(),
        } },
        { BlockType::Play, "play", "Song", "A song (or a piece of one) to play along to", BlockGroup::Play, true, {
            { "file", "Song", FieldKind::File, "A .chart in the lesson's folder, with its audio", "", CHART_FILES, 0, 0, true },
            goalField("The share of notes to hit, in percent (left out: 80)", 100),
            gateField(),
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
        { SectionLayout::WideNarrow, "wide-narrow", "Wide and narrow", { 0.62f, 0.38f } },
        { SectionLayout::NarrowWide, "narrow-wide", "Narrow and wide", { 0.38f, 0.62f } },
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

bool parseLessonDoc(const std::string& text, const std::string& path, LessonDoc& out, std::string& error){
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
            if (field.required && blockValues(*block, field.key).empty()) return lineError(std::string("a ") + info.id + " block needs '" + field.key + "'");
        if (block->type == BlockType::Exercise){
            const bool named = !blockValues(*block, "exercise").empty();
            if (named && !block->exerciseLines.empty())
                return lineError("an exercise block names an exercise or has one written in it, not both");
            if (!named && block->exerciseLines.empty())
                return lineError("an exercise block needs an exercise: 'exercise <file name>', or its settings written in it");
            if (!named){
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
            out.pages.push_back({ rest, {} });
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
    for (const LessonPage& page : doc.pages){
        out << "\npage" << (page.title.empty() ? "" : " " + oneLine(page.title)) << "\n";
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

bool saveLessonDoc(const std::string& folder, const LessonDoc& doc, std::string& error){
    std::error_code ec;
    fs::create_directories(folder, ec);
    return writeFileAtomically((fs::path(folder) / LESSON_FILE_NAME).string(), writeLessonDoc(doc), error);
}

// --- Lessons in folders --------------------------------------------------------------------------------------------

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
