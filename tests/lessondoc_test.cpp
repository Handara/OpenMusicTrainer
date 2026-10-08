#include "doctest/doctest.h"

#include "core/lessondoc.h"

#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

static const std::string HEAD = "# lahn lesson\nversion 2\ntitle E and F\n";

static const std::string FULL = HEAD + R"(category Reading
author Someone
description F, the top line.
instrument guitar

page Meet F
  section wide-narrow
    block text
      text F sits on the top line.
      text On the guitar: the 1st fret of the high E string.
  column
    block image
      file f.png
      caption F, on the top line

page Play it
    block exercise E and F
      type reading
      notes E4 F4
      cells quarter
      gate no
      bars 10
    block exercise
      exercise e-minor-open
      goal 2
    block play
      file riff.chart
      goal 90%
)";

TEST_CASE("lesson v2: pages, sections in their layouts, blocks in their columns"){
    LessonDoc doc;
    std::string error;
    REQUIRE_MESSAGE(parseLessonDoc(FULL, "full", doc, error), error);
    CHECK(doc.title == "E and F");
    CHECK(doc.category == "Reading");
    CHECK(doc.author == "Someone");
    CHECK(doc.instrument == ExerciseInstrument::Guitar);
    REQUIRE(doc.pages.size() == 2);

    const LessonPage& meet = doc.pages[0];
    CHECK(meet.title == "Meet F");
    REQUIRE(meet.sections.size() == 1);
    CHECK(meet.sections[0].layout == SectionLayout::WideNarrow);
    REQUIRE(meet.sections[0].columns.size() == 2);
    const LessonBlock& text = meet.sections[0].columns[0].at(0);
    CHECK(text.type == BlockType::Text);
    CHECK(blockValues(text, "text") == std::vector<std::string>{ "F sits on the top line.", "On the guitar: the 1st fret of the high E string." });
    const LessonBlock& image = meet.sections[0].columns[1].at(0);
    CHECK(blockValue(image, "file") == "f.png");
    CHECK(blockValue(image, "caption") == "F, on the top line");
    CHECK_FALSE(blockScored(image));

    // Blocks straight on a page go in one column
    const LessonPage& play = doc.pages[1];
    REQUIRE(play.sections.size() == 1);
    CHECK(play.sections[0].layout == SectionLayout::Single);
    REQUIRE(play.sections[0].columns[0].size() == 3);
    const LessonBlock& inPlace = play.sections[0].columns[0][0];
    CHECK(inPlace.name == "E and F");
    CHECK(inPlace.exercise.type == ExerciseType::Reading);
    CHECK(inPlace.exercise.title == "E and F");
    CHECK(inPlace.exercise.reading.bars == 10); // its settings after the block's own still its
    CHECK(inPlace.exerciseLines == std::vector<std::string>{ "type reading", "notes E4 F4", "cells quarter", "bars 10" });
    CHECK(blockScored(inPlace));
    CHECK_FALSE(blockGates(inPlace)); // gate no: there to practise
    const LessonBlock& named = play.sections[0].columns[0][1];
    CHECK(blockValue(named, "exercise") == "e-minor-open");
    CHECK(blockGoal(named) == 2);
    CHECK(blockGates(named)); // passed before going on, unless it says otherwise
    CHECK(blockGoal(play.sections[0].columns[0][2]) == 90);

    // In reading order
    const std::vector<BlockPlace> places = lessonBlocks(doc);
    REQUIRE(places.size() == 5);
    CHECK(blockAt(doc, places[1]).type == BlockType::Image);
    CHECK(places[1].column == 1);
    CHECK(places[4].page == 1);
}

TEST_CASE("lesson v2: written and read back, the same"){
    LessonDoc doc;
    std::string error;
    REQUIRE(parseLessonDoc(FULL, "full", doc, error));
    const std::string written = writeLessonDoc(doc);
    LessonDoc again;
    REQUIRE_MESSAGE(parseLessonDoc(written, "again", again, error), error);
    CHECK(writeLessonDoc(again) == written);
    CHECK(again.pages[1].sections[0].columns[0][0].exercise.reading.bars == 10);

    // Settings edited keep their place; a paragraph typed over two lines is written as two
    LessonBlock& text = again.pages[0].sections[0].columns[0][0];
    setBlockValues(text, "text", { "One.\nTwo." });
    LessonDoc third;
    REQUIRE(parseLessonDoc(writeLessonDoc(again), "third", third, error));
    CHECK(blockValues(third.pages[0].sections[0].columns[0][0], "text") == std::vector<std::string>{ "One.", "Two." });
    // An empty column is kept as one
    again.pages[0].sections[0].columns[0].clear();
    REQUIRE(parseLessonDoc(writeLessonDoc(again), "fourth", third, error));
    CHECK(third.pages[0].sections[0].columns[0].empty());
    CHECK(third.pages[0].sections[0].columns[1].size() == 1);
}

TEST_CASE("lesson v2: what's wrong is said, with its line"){
    struct Case { const char* name; std::string text; const char* expected; };
    const Case cases[] = {
        { "no version", "title T\npage P\n", "missing 'version'" },
        { "newer", "version 9\ntitle T\n", "newer than this build supports" },
        { "no title", "version 2\npage P\n", "missing 'title'" },
        { "no pages", HEAD, "at least one page" },
        { "unknown layout", HEAD + "page P\nsection wide\n", "unknown layout 'wide'" },
        { "too many columns", HEAD + "page P\nsection halves\ncolumn\ncolumn\n", ":7: a halves section has 2 columns" },
        { "column alone", HEAD + "page P\ncolumn\n", "'column' outside a section" },
        { "unknown block", HEAD + "page P\nblock chord\n", "unknown block 'chord'" },
        { "stray setting", HEAD + "page P\ntext Hello\n", "'text' outside a block" },
        { "wrong setting", HEAD + "page P\nblock image\nfile a.png\ntext Hi\n", "'text' doesn't belong in a image block" },
        { "needs its file", HEAD + "page P\nblock image\ncaption C\n", ":5: a image block needs 'file'" },
        { "a file's type", HEAD + "page P\nblock image\nfile a.gif\n", "isn't a file it takes" },
        { "outside the folder", HEAD + "page P\nblock image\nfile ../a.png\n", "must be a file in the lesson's folder" },
        { "twice", HEAD + "page P\nblock image\nfile a.png\nfile b.png\n", "'file' twice" },
        { "a toggle", HEAD + "page P\nblock play\nfile a.chart\ngate maybe\n", "'gate' is yes or no" },
        { "a number", HEAD + "page P\nblock play\nfile a.chart\ngoal 120\n", "'goal' is a number from 1 to 100" },
        { "no exercise", HEAD + "page P\nblock exercise\ngoal 2\n", "needs an exercise" },
        { "both", HEAD + "page P\nblock exercise\nexercise x\ntype notes\n", "not both" },
        { "the exercise's own", HEAD + "page P\nblock exercise\ntype notes\nnotes E4\ncount zero\n", ":8: count must be" },
        { "a routine", HEAD + "page P\nblock exercise\ntype routine\nstep x 3\n", "a routine can't be a block" },
        { "instrument", HEAD + "instrument kazoo\n", "instrument must be" },
    };
    for (const Case& c : cases){
        SUBCASE(c.name){
            LessonDoc doc;
            std::string error;
            CHECK_FALSE(parseLessonDoc(c.text, "bad", doc, error));
            CHECK_MESSAGE(error.find(c.expected) != std::string::npos, error);
        }
    }
}

TEST_CASE("lesson v2: the neck, keys and staff blocks' settings, checked"){
    LessonDoc doc;
    std::string error;
    const std::string good = HEAD + "page P\n"
        "block fretboard\nfrets 0 3\ndots 6:0 6:1:F 5:3:2\nlit 6:1\n"
        "block keyboard\nfrom C4\nto G5\nlit C4 E4 G4\n"
        "block staff\nnotes E4 F4 G4\nkey G major\n"
        "block reveal\ntext It's an [F4].\n";
    REQUIRE_MESSAGE(parseLessonDoc(good, "good", doc, error), error);
    const LessonBlock& neck = doc.pages[0].sections[0].columns[0][0];
    const std::vector<NeckPlace> dots = readNeckPlaces(blockValue(neck, "dots"));
    REQUIRE(dots.size() == 3);
    CHECK(dots[0].string == 5); // 6 = the highest string of six, counted from 0
    CHECK(dots[0].fret == 0);
    CHECK(dots[1].label == "F");
    CHECK(dots[2].label == "2");
    CHECK(blockValue(neck, "labels") == "names"); // its standard
    CHECK(readNotes(blockValue(doc.pages[0].sections[0].columns[0][1], "lit")) == std::vector<int>{ 60, 64, 67 });
    CHECK(blockValue(doc.pages[0].sections[0].columns[0][3], "label") == "Show the answer");

    struct Case { const char* name; std::string text; const char* expected; };
    const Case cases[] = {
        { "a place", HEAD + "page P\nblock fretboard\ndots 6-1\n", "isn't a place" },
        { "string 0", HEAD + "page P\nblock fretboard\ndots 0:1\n", "isn't a place" },
        { "frets", HEAD + "page P\nblock fretboard\nfrets 5 2\n", "'frets' is two numbers from 0 to 24" },
        { "a note", HEAD + "page P\nblock keyboard\nfrom H4\n", "'H4' isn't a note" },
        { "notes", HEAD + "page P\nblock staff\nnotes E4 X\n", "'X' isn't a note" },
        { "a key", HEAD + "page P\nblock staff\nnotes E4\nkey G dorian\n", "isn't a key" },
        { "staff needs notes", HEAD + "page P\nblock staff\nkey G major\n", "needs 'notes'" },
    };
    for (const Case& c : cases){
        SUBCASE(c.name){
            CHECK_FALSE(parseLessonDoc(c.text, "bad", doc, error));
            CHECK_MESSAGE(error.find(c.expected) != std::string::npos, error);
        }
    }
}

TEST_CASE("lesson v1: its steps read as pages, a block each"){
    const std::string v1 = "version 1\ntitle First chords\nauthor Someone\n"
                           "step text\ntitle What is a chord?\ntext Three notes or more.\n"
                           "step text\ntitle Just a heading\n"
                           "step image\nfile em.png\ncaption E minor\n"
                           "step exercise\nexercise e-minor-open\ngoal 2\n"
                           "step play\nfile riff.chart\ngoal 90\n";
    LessonDoc doc;
    std::string error;
    REQUIRE_MESSAGE(parseLessonDoc(v1, "old", doc, error), error);
    CHECK(doc.title == "First chords");
    CHECK(doc.author == "Someone");
    REQUIRE(doc.pages.size() == 5);
    CHECK(doc.pages[0].title == "What is a chord?");
    CHECK(blockValues(doc.pages[0].sections[0].columns[0].at(0), "text") == std::vector<std::string>{ "Three notes or more." });
    CHECK(doc.pages[1].sections[0].columns[0].empty()); // a heading alone: the page's title says it
    CHECK(blockValue(doc.pages[2].sections[0].columns[0].at(0), "caption") == "E minor");
    const LessonBlock& exercise = doc.pages[3].sections[0].columns[0].at(0);
    CHECK(blockValue(exercise, "exercise") == "e-minor-open");
    CHECK(blockGoal(exercise) == 2);
    CHECK(blockGates(exercise)); // a v1 exercise is passed before going on
    // Written as v2, it reads back the same
    LessonDoc again;
    REQUIRE_MESSAGE(parseLessonDoc(writeLessonDoc(doc), "migrated", again, error), error);
    CHECK(writeLessonDoc(again) == writeLessonDoc(doc));
}

TEST_CASE("lesson v2 files: saved and loaded, media checked in its folder"){
    const fs::path folder = fs::temp_directory_path() / "lahn_tests" / "lessondoc" / "full";
    fs::remove_all(folder);
    LessonDoc doc;
    std::string error;
    REQUIRE(parseLessonDoc(FULL, "full", doc, error));
    REQUIRE(saveLessonDoc(folder.string(), doc, error));
    LessonDoc loaded;
    CHECK_FALSE(loadLessonDoc(folder.string(), loaded, error)); // its picture and song aren't there yet
    CHECK(error.find("'f.png' isn't in the lesson's folder") != std::string::npos);
    std::ofstream(folder / "f.png") << "x";
    std::ofstream(folder / "riff.chart") << "version 1\n"; // not a chart that loads
    CHECK_FALSE(loadLessonDoc(folder.string(), loaded, error));
    CHECK(error.find("play block") != std::string::npos);
    std::ofstream(folder / "riff.chart") << "version 2\nresolution 480\nend 1920\ntempo 0 120\ntrack guitar Lead\ntuning 40 45 50 55 59 64\nn 0 0 0\n";
    REQUIRE_MESSAGE(loadLessonDoc(folder.string(), loaded, error), error);
    CHECK(writeLessonDoc(loaded) == writeLessonDoc(doc));
}

TEST_CASE("every block's settings are described, for the editor"){
    for (const BlockInfo& info : blockInfos()){
        CHECK(findBlockInfo(info.id) == &info);
        CHECK(std::string(info.name).size() > 0);
        if (info.scored) CHECK(findBlockField(info.type, "gate") != nullptr); // every scored block can be made optional
        for (const BlockField& field : info.fields){
            CHECK(std::string(field.description).size() > 0);
            if (field.kind == FieldKind::File) CHECK_FALSE(field.choices.empty());
            if (field.kind == FieldKind::Number) CHECK(field.max >= field.min);
        }
    }
    for (int layout = 0; layout < (int)SectionLayout::Count; layout++){
        float total = 0.0f;
        for (float share : sectionShares((SectionLayout)layout)) total += share;
        CHECK(total == doctest::Approx(1.0f));
        CHECK(sectionColumns((SectionLayout)layout) == (int)sectionShares((SectionLayout)layout).size());
    }
}

TEST_CASE("making lessons: blocks added, moved and taken out, layouts changed"){
    LessonDoc doc;
    doc.title = "T";
    doc.pages.push_back({ "P", { makeSection(SectionLayout::Halves) } });
    auto text = [](const char* words){ LessonBlock block = makeBlock(BlockType::Text); setBlockValues(block, "text", { words }); return block; };
    REQUIRE(insertBlock(doc, { 0, 0, 0, 0 }, text("a")));
    REQUIRE(insertBlock(doc, { 0, 0, 0, 1 }, text("b")));
    REQUIRE(insertBlock(doc, { 0, 0, 0, 2 }, text("c")));
    CHECK_FALSE(insertBlock(doc, { 0, 0, 0, 9 }, text("x"))); // past the column's end
    CHECK_FALSE(insertBlock(doc, { 0, 0, 2, 0 }, text("x"))); // a column it hasn't got
    auto words = [&](int column){
        std::string all;
        for (const LessonBlock& block : doc.pages[0].sections[0].columns[(size_t)column]) all += blockValue(block, "text");
        return all;
    };
    CHECK(words(0) == "abc");
    // Down its own column: before the block at 3 (the end) is after c
    BlockPlace landed = moveBlock(doc, { 0, 0, 0, 0 }, { 0, 0, 0, 3 });
    CHECK(words(0) == "bca");
    CHECK(landed.block == 2);
    // Where it is already: nothing moves
    CHECK(moveBlock(doc, { 0, 0, 0, 1 }, { 0, 0, 0, 2 }).block == 1);
    CHECK(words(0) == "bca");
    // To the other column
    landed = moveBlock(doc, { 0, 0, 0, 1 }, { 0, 0, 1, 0 });
    CHECK(words(0) == "ba");
    CHECK(words(1) == "c");
    CHECK(landed.column == 1);
    CHECK(removeBlock(doc, { 0, 0, 0, 0 }));
    CHECK(words(0) == "a");
    CHECK(blockPointer(doc, { 0, 0, 0, 5 }) == nullptr);
    // One column now: the second's blocks join the first's end
    setSectionLayout(doc.pages[0].sections[0], SectionLayout::Single);
    CHECK(doc.pages[0].sections[0].columns.size() == 1);
    CHECK(words(0) == "ac");
    setSectionLayout(doc.pages[0].sections[0], SectionLayout::Thirds);
    CHECK(doc.pages[0].sections[0].columns.size() == 3);
}

TEST_CASE("making lessons: new blocks show something, exercises written in them are read"){
    for (int type = 0; type < (int)BlockType::Count; type++){
        const LessonBlock block = makeBlock((BlockType)type);
        CHECK(block.type == (BlockType)type);
        for (const auto& [key, value] : block.values) CHECK_MESSAGE(checkBlockValue(*findBlockField(block.type, key), value).empty(), key);
    }
    LessonBlock exercise = makeBlock(BlockType::Exercise);
    exercise.name = "E and F";
    setBlockValue(exercise, "exercise", "e-minor-open");
    std::string error;
    REQUIRE_MESSAGE(setBlockExercise(exercise, "type notes\n  notes E4 F4\n\n# a comment\ncount 6\n", error), error);
    CHECK(exercise.exerciseLines == std::vector<std::string>{ "type notes", "notes E4 F4", "count 6" });
    CHECK(exercise.exercise.title == "E and F");
    CHECK(blockValue(exercise, "exercise").empty()); // written in it now: not named
    CHECK_FALSE(setBlockExercise(exercise, "type notes\nnotes H4\n", error));
    CHECK(exercise.exerciseLines.size() == 3); // left as it was
    CHECK_FALSE(setBlockExercise(exercise, "type notes\ngate no\n", error));
    CHECK(error.find("the block's own setting") != std::string::npos);
    CHECK_FALSE(setBlockExercise(exercise, "\n", error));
}

TEST_CASE("exercises written in blocks, as forms: every kind starts from something that reads, a setting at a time"){
    for (const ExerciseForm& form : exerciseForms()){
        LessonBlock block = makeBlock(BlockType::Exercise);
        std::string error;
        const bool reads = setBlockExercise(block, form.starter, error);
        CHECK_MESSAGE(reads, form.type << ": " << error);
        CHECK(exerciseSetting(block, "type") == form.type);
        CHECK(findExerciseForm(form.type) == &form);
        for (const BlockField& field : form.fields) CHECK(std::string(field.description).size() > 0);
    }
    LessonBlock block = makeBlock(BlockType::Exercise);
    std::string error;
    REQUIRE(setBlockExercise(block, findExerciseForm("notes")->starter, error));
    REQUIRE(setExerciseSetting(block, "count", "6", error));
    CHECK(exerciseSetting(block, "count") == "6");
    CHECK(block.exercise.noteQuiz.count == 6);
    REQUIRE(setExerciseSetting(block, "notes", "C4 D4", error)); // changed in its place
    CHECK(block.exerciseLines[1] == "notes C4 D4");
    CHECK_FALSE(setExerciseSetting(block, "count", "lots", error)); // the exercise's own reader says no
    CHECK(exerciseSetting(block, "count") == "6");                 // and it's left as it was
    REQUIRE(setExerciseSetting(block, "count", "", error));         // taken out: its standard again
    CHECK(exerciseSetting(block, "count").empty());
}

TEST_CASE("a draft: blocks still missing what they need are kept, and read back"){
    LessonDoc doc;
    doc.title = "Draft";
    doc.pages.push_back({ "P", { makeSection(SectionLayout::Single) } });
    insertBlock(doc, { 0, 0, 0, 0 }, makeBlock(BlockType::Image));    // no picture chosen yet
    insertBlock(doc, { 0, 0, 0, 1 }, makeBlock(BlockType::Exercise)); // no exercise yet
    const std::string written = writeLessonDoc(doc);
    LessonDoc read;
    std::string error;
    CHECK_FALSE(parseLessonDoc(written, "strict", read, error)); // not to be played like this
    REQUIRE_MESSAGE(parseLessonDoc(written, "draft", read, error, true), error);
    CHECK(read.pages[0].sections[0].columns[0].size() == 2);
    CHECK(writeLessonDoc(read) == written);
    // Anything else wrong is still wrong
    CHECK_FALSE(parseLessonDoc(written + "  block nothing\n", "draft", read, error, true));
}

TEST_CASE("the built-in lessons load"){
    const std::vector<LessonEntry> lessons = scanLessons(std::string(LAHN_RESOURCES_DIR) + "lessons", true);
    CHECK_FALSE(lessons.empty());
    for (const LessonEntry& entry : lessons){
        CHECK_MESSAGE(entry.error.empty(), entry.error);
        CHECK(entry.version == LESSON_DOC_VERSION);
    }
}
