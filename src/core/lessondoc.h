#pragma once

#include "core/exercisefile.h"
#include "core/lesson.h"

#include <string>
#include <utility>
#include <vector>

// Lessons, version 2: pages of blocks, laid out in sections. Anything the game can show or do is a block (a text, a
// picture, a drill, a song to play...); a section lays its blocks out in columns, in one of a few fixed layouts, so a
// lesson looks right at any size, whoever made it. The student goes through it a page at a time. A lesson is a
// folder, as before: lesson.lesson and its media next to it, written as text anyone can make and share.
//
//   # lahn lesson
//   version 2
//   title E and F
//   category Reading
//   author Someone
//   description F, the top line of the staff.
//   instrument guitar             (optional: guitar, bass or piano; any if left out)
//
//   page Meet F                   (a page, with its title)
//     section wide-narrow         (its blocks in columns: single, halves, wide-narrow, narrow-wide or thirds)
//       block text
//         text F sits on the top line.
//       column                    (the next column)
//       block image
//         file f-on-the-staff.png
//         caption F, on the top line
//
//   page Play it
//     block exercise E and F      (blocks before any section go in a single column; an exercise written in place:
//       type reading               its settings follow, as in an .exercise file, its title the block's name)
//       notes E4 F4
//       gate yes                  (a block that's scored: passed before going on (the default), or "no": optional)
//
// Indenting is only for reading: it's ignored. Every kind of block, and what it takes, is described by blockInfos(): the
// file's reader, its checks, its writer and the editor all go by it. Version 1 lessons (steps) are read as version 2,
// a page each.

// --- Blocks -------------------------------------------------------------------------------------------------------

enum class BlockType { Text, Image, Audio, Video, Exercise, Play, Count };
enum class BlockGroup { Show, Play, Hear, Games, Smart }; // where the editor offers it

// A setting a block takes
enum class FieldKind {
    Paragraphs, // text: the key once per paragraph
    Text,       // a line of text
    File,       // a file in the lesson's folder, of the types given
    Number,     // a whole number, from min to max
    Toggle,     // yes or no
    Choice,     // one of the choices
    Exercise,   // an exercise by its file name (without .exercise)
};

struct BlockField {
    const char* key;          // in the file: "caption"
    const char* name;         // in the editor: "Caption"
    FieldKind kind;
    const char* description;  // what it does, for the editor
    const char* standard = ""; // its value when it isn't written ("" for none)
    std::vector<std::string> choices = {}; // a choice's, or a file's types (".png")
    int min = 0, max = 0;     // a number's
    bool required = false;
};

struct BlockInfo {
    BlockType type;
    const char* id;           // in the file: "image"
    const char* name;         // in the editor: "Picture"
    const char* description;
    BlockGroup group;
    bool scored;              // passed or not (a drill, a song): it may hold the student back until it's passed
    std::vector<BlockField> fields;
};
const std::vector<BlockInfo>& blockInfos();
const BlockInfo& blockInfo(BlockType type);
const BlockInfo* findBlockInfo(const std::string& id);
const BlockField* findBlockField(BlockType type, const std::string& key);

struct LessonBlock {
    BlockType type = BlockType::Text;
    std::string name;  // optional: what it's called (a drill's name in the list of the lesson's drills)
    // Its settings as written, in order: a paragraph's key comes once per paragraph. Read with blockValue(s).
    std::vector<std::pair<std::string, std::string>> values;
    // An exercise block written in place: its settings as written (kept to be written back as they are), and as read
    std::vector<std::string> exerciseLines;
    ExerciseFile exercise;
};

// A setting's value: as written, or else its standard one
std::string blockValue(const LessonBlock& block, const std::string& key);
std::vector<std::string> blockValues(const LessonBlock& block, const std::string& key); // every one (paragraphs)
void setBlockValue(LessonBlock& block, const std::string& key, const std::string& value); // replaces; "" removes it
void setBlockValues(LessonBlock& block, const std::string& key, const std::vector<std::string>& values);
bool blockScored(const LessonBlock& block);
bool blockGates(const LessonBlock& block); // scored, and passed before going on
int blockGoal(const LessonBlock& block);   // its goal (0: the usual one for what it runs: lessonGoal)

// --- Sections and pages -----------------------------------------------------------------------------------------

enum class SectionLayout { Single, Halves, WideNarrow, NarrowWide, Thirds, Count };
const char* sectionLayoutId(SectionLayout layout);      // "wide-narrow"
const char* sectionLayoutName(SectionLayout layout);    // "Wide and narrow"
int sectionColumns(SectionLayout layout);
std::vector<float> sectionShares(SectionLayout layout); // each column's share of the width, adding up to 1

struct LessonSection {
    SectionLayout layout = SectionLayout::Single;
    std::vector<std::vector<LessonBlock>> columns; // as many as the layout has
};
LessonSection makeSection(SectionLayout layout);

struct LessonPage {
    std::string title;
    std::vector<LessonSection> sections;
};

struct LessonDoc {
    std::string title;
    std::string category = "Other";
    std::string author;
    std::string description;
    ExerciseInstrument instrument = ExerciseInstrument::Any;
    std::vector<LessonPage> pages;
};

// Every block, page by page, in reading order (section by section, column by column)
struct BlockPlace {
    int page = 0, section = 0, column = 0, block = 0;
};
std::vector<BlockPlace> lessonBlocks(const LessonDoc& doc);
const LessonBlock& blockAt(const LessonDoc& doc, const BlockPlace& place);

// --- Files --------------------------------------------------------------------------------------------------------

const int LESSON_DOC_VERSION = 2;

// Strict, like charts and exercises: lessons are shared. `path` names it in errors. Version 1 is read too (a page
// a step).
bool parseLessonDoc(const std::string& text, const std::string& path, LessonDoc& out, std::string& error);
std::string writeLessonDoc(const LessonDoc& doc);
// A version 1 lesson's steps, a page each
LessonDoc lessonDocFromSteps(const Lesson& lesson);

// From a lesson's folder: the file read, then its media checked (each file in the folder, a play block's chart loading)
bool loadLessonDoc(const std::string& folder, LessonDoc& out, std::string& error);
bool checkLessonFiles(const LessonDoc& doc, const std::string& folder, std::string& error);
bool saveLessonDoc(const std::string& folder, const LessonDoc& doc, std::string& error);
