#pragma once

#include "core/chart.h"
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

enum class BlockType { Text, Heading, Callout, Reveal, Fretboard, Keyboard, Staff, Image, Audio, Video, Exercise, Play, Practice, Count };
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
    Note,       // a note by its name: E4, F#3, Bb2
    Notes,      // notes by their names, apart
    Places,     // places on the neck, apart: string:fret (1 = the lowest string), and a label if wanted (6:1:F)
    Span,       // two whole numbers, the lower first, from min to max
    Key,        // a key: its note and major or minor (G major, E minor)
    Song,       // a song of the game's, by its folder's name
    Page,       // a page of the lesson, by its title
    Music,      // notes with their lengths, and rests: E4/8 F4/8 G4/2. r/4 C4+E4/4 (staffMusic below)
};

// A place on the neck, as a Places setting writes it
struct NeckPlace {
    int string = 0; // 0 = the lowest
    int fret = 0;
    std::string label;
};
std::vector<NeckPlace> readNeckPlaces(const std::string& value); // the ones that read right

// A staff's music as a staff block writes it: notes (one, or several at once joined by +) and rests (r), each its
// length after a slash: 1 a whole, 2 a half, 4 a quarter (left out: a quarter), 8 an eighth, 16 a sixteenth; a dot
// after it adds half again, a t makes it a triplet's (three in the time of two). "E4/8 F4/8 G4/2. r/4 C4+E4+G4/2".
// Beams, ties over bar lines and rests are the engraver's (core/score), as for any music.
struct StaffEvent {
    std::vector<int> pitches; // empty: a rest
    int ticks = 480;          // its length: 480 a beat (a quarter)
};
const int STAFF_TICKS_A_BEAT = 480;
bool readStaffMusic(const std::string& text, std::vector<StaffEvent>& out, std::string& error);
std::string writeStaffEvent(const StaffEvent& event); // one: "E4/8.", "r/2", "C4+E4" (a quarter)
std::string writeStaffMusic(const std::vector<StaffEvent>& events);
// The music as a chart, to be engraved: on this instrument's tuning (a piano's {0}), in this key and meter (x/4)
Chart staffChart(const std::vector<StaffEvent>& events, const std::vector<int>& tuning, const KeySignature& key, int beatsPerBar);
std::vector<int> readNotes(const std::string& value);            //   (MIDI)

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
    bool aside = false; // a page of help: skipped on the way through, shown only when a block sends the student there
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

int findPage(const LessonDoc& doc, const std::string& title); // by its title: -1 for none

// Every block, page by page, in reading order (section by section, column by column)
struct BlockPlace {
    int page = 0, section = 0, column = 0, block = 0;
};
std::vector<BlockPlace> lessonBlocks(const LessonDoc& doc);
const LessonBlock& blockAt(const LessonDoc& doc, const BlockPlace& place);

// --- Files --------------------------------------------------------------------------------------------------------

const int LESSON_DOC_VERSION = 2;

// Strict, like charts and exercises: lessons are shared. `path` names it in errors. Version 1 is read too (a page
// a step). A draft (the lesson maker's: a lesson being made) may have blocks still missing what they need (a picture
// not chosen yet, an exercise not picked): everything else is as strict.
bool parseLessonDoc(const std::string& text, const std::string& path, LessonDoc& out, std::string& error, bool draft = false);
std::string writeLessonDoc(const LessonDoc& doc);
// Its pages only (what a course writes for each chapter, under the chapter's own line)
std::string writeLessonPages(const LessonDoc& doc);
// A version 1 lesson's steps, a page each
LessonDoc lessonDocFromSteps(const Lesson& lesson);

// From a lesson's folder: the file read, then its media checked (each file in the folder, a play block's chart loading)
bool loadLessonDoc(const std::string& folder, LessonDoc& out, std::string& error);
bool loadLessonDraft(const std::string& folder, LessonDoc& out, std::string& error); // a draft, its media not checked
bool checkLessonFiles(const LessonDoc& doc, const std::string& folder, std::string& error);
bool saveLessonDoc(const std::string& folder, const LessonDoc& doc, std::string& error);

// --- Making lessons (the lesson maker's changes) -----------------------------------------------------------------

// A new block of a kind, with something in it to show (a starter text, notes on a neck...): what's added is seen
LessonBlock makeBlock(BlockType type);
// A setting's value checked against what it takes: "" when it's fine, else what's wrong
std::string checkBlockValue(const BlockField& field, const std::string& value);
// An exercise written in a block: its settings, a line each, read as one (false and why when they don't read)
bool setBlockExercise(LessonBlock& block, const std::string& lines, std::string& error);

// An exercise written in a block, as a form: each kind of exercise, what it's called and its settings (read and
// checked by the exercise's own reader, core/exercisefile); a starter to begin one from
struct ExerciseForm {
    const char* type;        // as its 'type' line says it: "notes"
    const char* name;        // "Play the notes"
    const char* description;
    const char* starter;     // its settings to begin from, a line each (type first)
    std::vector<BlockField> fields;
};
const std::vector<ExerciseForm>& exerciseForms();
const ExerciseForm* findExerciseForm(const std::string& type);
// One setting of an exercise written in a block: its value ("" when it isn't there), and changed (an empty value
// takes it out). False, and why, when the exercise doesn't read with it: the block is left as it was.
std::string exerciseSetting(const LessonBlock& block, const std::string& key);
bool setExerciseSetting(LessonBlock& block, const std::string& key, const std::string& value, std::string& error);

// Where a block goes: a page's section's column, before the block at `block` (the column's size: at its end)
LessonBlock* blockPointer(LessonDoc& doc, const BlockPlace& place); // nullptr for no such block
bool insertBlock(LessonDoc& doc, const BlockPlace& at, const LessonBlock& block);
bool removeBlock(LessonDoc& doc, const BlockPlace& place);
// Moved to before the block at `to` (as the column is before it's taken out); returns where it ended up, or `from`
// when it can't go there
BlockPlace moveBlock(LessonDoc& doc, const BlockPlace& from, const BlockPlace& to);
// Marking by clicking (the maker's): a neck's place goes from nothing to a note, to a lit note, to nothing again (a
// label it has goes with it); a piano's key, lit or not
void cycleNeckPlace(LessonBlock& block, int string, int fret);
void toggleLitKey(LessonBlock& block, int pitch);

// A new layout: blocks of columns it no longer has go to the end of its last
void setSectionLayout(LessonSection& section, SectionLayout layout);

// --- Templates: something to start from -------------------------------------------------------------------------

struct LessonTemplate {
    const char* id;          // "new-note"
    const char* name;        // "A new note"
    const char* description;
};
// Pages to add: blank, explain, a drill, a song, a question, help (set aside)
const std::vector<LessonTemplate>& pageTemplates();
LessonPage makePage(const std::string& templateId, ExerciseInstrument instrument);
// Whole lessons: blank, a new note, a riff, ear training
const std::vector<LessonTemplate>& lessonTemplates();
LessonDoc makeLesson(const std::string& templateId, const std::string& title, ExerciseInstrument instrument);

// --- Lessons in folders --------------------------------------------------------------------------------------------

struct LessonEntry {
    std::string folder;
    std::string id;     // names its progress file: "builtin-<folder name>" or "user-<folder name>"
    bool builtIn;
    int version = 0;    // its file's: 1 (steps) or 2 (pages)
    LessonDoc doc;      // the lesson
    Lesson lesson;      // its title, category, author and description (if it failed to load, only the title: the
                        // folder's name); a version 1 lesson's steps too, for the editor of those
    std::string error;  // why it can't be played, empty if it can
};

// Every folder with a lesson.lesson in it, sorted by category then title
std::vector<LessonEntry> scanLessons(const std::string& dir, bool builtIn);
// Exercise blocks may name exercises: marks lessons whose exercise is missing, broken, or a routine (a routine has no
// goal a lesson could check)
void checkLessonExercises(std::vector<LessonEntry>& lessons, const std::vector<ExerciseEntry>& exercises);

// A practice block's exercise, made as it's played: from these notes (the student's weakest, or the lesson's own), as
// notes to play or read to a beat, on the lesson's instrument; its settings, a line each
std::string practiceExercise(const LessonBlock& block, const std::vector<int>& pitches, ExerciseInstrument instrument);
// Every note the lesson asks for or shows (its exercises' notes, its staffs' and keyboards'), low to high
std::vector<int> lessonNotes(const LessonDoc& doc);

// A song block's song: a song of the game's (its folder's name, found in these folders of songs), else a chart in the
// lesson's folder. "" when it's neither, or the song isn't there.
std::string songBlockChart(const LessonBlock& block, const std::string& lessonFolder, const std::vector<std::string>& songFolders);
// Marks lessons whose song blocks name songs that aren't in these folders
void checkLessonSongs(std::vector<LessonEntry>& lessons, const std::vector<std::string>& songFolders);

// A scored block's place in its lesson's progress (LessonProgress::passed): its page's, and which of the page's
// scored blocks it is. (A version 1 lesson kept a step's number there: read with the lesson, it's that step's page.)
int scoredBlockKey(const LessonDoc& doc, const BlockPlace& place);
std::vector<BlockPlace> scoredBlocks(const LessonDoc& doc, int page); // a page's, in reading order
void upgradeLessonProgress(LessonProgress& progress, int version);    // a version 1 lesson's, to these keys
// What a scored block takes to pass: its goal, or the usual one for what it runs ("Goal: 2 clean passes")
int scoredBlockGoal(const LessonBlock& block, ExerciseType exerciseType);
std::string scoredBlockGoalText(const LessonBlock& block, const ExerciseEntry* exercise);
