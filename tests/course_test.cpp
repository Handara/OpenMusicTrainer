#include "doctest/doctest.h"

#include "core/course.h"

#include <filesystem>
#include <string>

static const char* const SAMPLE =
    "# lahn course\n"
    "version 1\n"
    "title Reading music\n"
    "description A little at a time.\n"
    "instrument guitar\n"
    "\n"
    "unit The high E string\n"
    "\n"
    "lesson Your first note\n"
    "title The staff\n"
    "text Five lines.\n"
    "text Notes sit on them.\n"
    "exercise\n"
    "type notes\n"
    "notes E4\n"
    "show staff\n"
    "goal 2\n"
    "count 4\n"
    "\n"
    "lesson E and F\n"
    "text Now two.\n"
    "exercise reading-first-notes\n"
    "\n"
    "unit The B string\n"
    "lesson B\n"
    "exercise\n"
    "type notes\n"
    "notes B3\n";

TEST_CASE("courses: units of small lessons, text and exercises written in place or named"){
    Course course;
    std::string error;
    REQUIRE_MESSAGE(parseCourse(SAMPLE, "sample.course", course, error), error);
    CHECK(course.title == "Reading music");
    CHECK(course.instrument == ExerciseInstrument::Guitar);
    REQUIRE(course.units.size() == 2);
    REQUIRE(course.lessons.size() == 3);
    CHECK(course.units[0].lessonCount == 2);
    CHECK(course.units[1].firstLesson == 2);
    CHECK(course.lessons[2].unit == 1);
    CHECK(course.lessons[0].id == "your-first-note");
    // The first lesson: a text step (its heading and two paragraphs), then an exercise written in place
    const Lesson& first = course.lessons[0].lesson;
    CHECK(first.title == "Your first note");
    REQUIRE(first.steps.size() == 2);
    CHECK(first.steps[0].type == LessonStepType::Text);
    CHECK(first.steps[0].title == "The staff");
    CHECK(first.steps[0].paragraphs.size() == 2);
    const LessonStep& exercise = first.steps[1];
    CHECK(exercise.type == LessonStepType::Exercise);
    CHECK(exercise.inlined);
    CHECK(exercise.goal == 2);
    CHECK(exercise.inlineExercise.type == ExerciseType::Notes);
    CHECK(exercise.inlineExercise.title == "Your first note"); // the lesson's
    CHECK(exercise.inlineExercise.noteQuiz.count == 4);        // read after the goal: the block goes on
    REQUIRE(exercise.inlineExercise.noteQuiz.notes.size() == 1);
    CHECK(exercise.inlineExercise.noteQuiz.notes[0].pitch == 64);
    CHECK(exercise.inlineExercise.noteQuiz.prompt == NotePrompt::Staff);
    // The second: named
    const Lesson& second = course.lessons[1].lesson;
    REQUIRE(second.steps.size() == 2);
    CHECK_FALSE(second.steps[1].inlined);
    CHECK(second.steps[1].exercise == "reading-first-notes");
}

TEST_CASE("courses: a bass course's exercises are played on the bass"){
    std::string text = SAMPLE;
    text.replace(text.find("instrument guitar"), 17, "instrument bass");
    text.replace(text.find("notes B3"), 8, "notes B1");
    Course course;
    std::string error;
    REQUIRE_MESSAGE(parseCourse(text, "bass.course", course, error), error);
    CHECK(course.instrument == ExerciseInstrument::Bass);
    const ExerciseFile& exercise = course.lessons[2].lesson.steps[0].inlineExercise;
    CHECK(exercise.neckOnBass);
    REQUIRE(exercise.noteQuiz.notes.size() == 1);
    CHECK(exercise.noteQuiz.notes[0].string == 1); // B1: the bass's A string, 2nd fret (the lowest place)
    CHECK(exercise.noteQuiz.notes[0].fret == 2);
}

TEST_CASE("courses: a piano course's notes are its keys"){
    std::string text = SAMPLE;
    text.replace(text.find("instrument guitar"), 17, "instrument piano");
    text.replace(text.find("notes B3"), 8, "notes C4");
    Course course;
    std::string error;
    REQUIRE_MESSAGE(parseCourse(text, "piano.course", course, error), error);
    CHECK(course.instrument == ExerciseInstrument::Piano);
    const ExerciseFile& exercise = course.lessons[2].lesson.steps[0].inlineExercise;
    CHECK(exercise.instrument == ExerciseInstrument::Piano);
    CHECK(exercise.noteQuiz.piano);
    REQUIRE(exercise.noteQuiz.notes.size() == 1);
    CHECK(exercise.noteQuiz.notes[0].pitch == 60);
    CHECK(exercise.noteQuiz.notes[0].fret == 60); // one "string" tuned to 0: the fret is the key
    // No strings to choose on a piano
    text.replace(text.find("notes C4"), 8, "places 1:3");
    CHECK_FALSE(parseCourse(text, "piano.course", course, error));
}

TEST_CASE("courses: mistakes are told by their line"){
    Course course;
    std::string error;
    // An exercise's own mistake, on its line in the course
    std::string text = SAMPLE;
    text.replace(text.find("show staff"), 10, "show tab");
    CHECK_FALSE(parseCourse(text, "c.course", course, error));
    CHECK(error.find("c.course:16:") != std::string::npos);
    // Two lessons of one title
    text = SAMPLE;
    text.replace(text.find("lesson E and F"), 14, "lesson Your first note");
    CHECK_FALSE(parseCourse(text, "c.course", course, error));
    CHECK(error.find("own titles") != std::string::npos);
    // A lesson with nothing in it, a unit with no lessons
    CHECK_FALSE(parseCourse("version 1\ntitle T\nunit U\nlesson L\n", "c.course", course, error));
    CHECK(error.find("no steps") != std::string::npos);
    CHECK_FALSE(parseCourse("version 1\ntitle T\nunit U\nlesson L\ntext x\nunit V\n", "c.course", course, error));
    CHECK(error.find("no lessons") != std::string::npos);
    // Unknown things
    CHECK_FALSE(parseCourse("version 1\ntitle T\nunit U\nlesson L\nvideo x.mp4\n", "c.course", course, error));
    CHECK(error.find(":5:") != std::string::npos);
    CHECK(courseSlug("E, F & G!") == "e-f-g");
}

TEST_CASE("courses: the ones shipped all load"){
    for (const CourseEntry& entry : scanCourses(std::string(LAHN_RESOURCES_DIR) + "courses")){
        CAPTURE(entry.path);
        CHECK(entry.error.empty());
        if (!entry.error.empty()) MESSAGE(entry.error);
    }
}

TEST_CASE("courses: drills scored by their best run; a chapter passed with all of them, then the next opens"){
    Course course;
    std::string error;
    REQUIRE_MESSAGE(parseCourse("version 1\ntitle T\n"
                                "unit Level one\n"
                                "lesson First\n"
                                "text Hello.\n"
                                "drill E alone\ntype notes\nnotes E4\ncount 4\npass 3\n"
                                "drill E and F\ntype notes\nnotes E4 F4\ncount 8\npass 8\n"
                                "lesson Second\n"
                                "exercise\ntype notes\nnotes G4\ncount 5\npass 4\n"
                                "unit Level two\n"
                                "lesson Words only\n"
                                "text Read me.\n",
                                "t.course", course, error), error);
    const std::vector<CourseDrill> drills = courseDrills(course, 0);
    REQUIRE(drills.size() == 2);
    CHECK(drills[0].name == "E alone");
    CHECK(drills[0].id == "first-2");       // the second step (the text is the first)
    CHECK(drills[0].passPercent == 75);     // 3 of 4
    CHECK(drills[1].passPercent == 100);
    CHECK(course.lessons[0].lesson.steps[1].inlineExercise.title == "E alone");
    CHECK(courseDrills(course, 1)[0].name == "Second"); // no name of its own: the chapter's
    CourseScores scores;
    CHECK_FALSE(chapterOpen(course, 1, scores));
    CHECK(courseContinue(course, scores) == 0);
    CHECK(recordCourseScore(scores, "first-2", 75));
    CHECK_FALSE(recordCourseScore(scores, "first-2", 50)); // not a best
    CHECK(recordCourseScore(scores, "first-3", 90));
    ChapterState first = chapterState(course, 0, scores);
    CHECK(first.percent == 82);
    CHECK_FALSE(first.passed); // 90 of the 100 needed
    recordCourseScore(scores, "first-3", 100);
    first = chapterState(course, 0, scores);
    CHECK(first.passed);
    CHECK_FALSE(first.perfect);
    CHECK(chapterOpen(course, 1, scores));
    CHECK(courseContinue(course, scores) == 1);
    recordCourseScore(scores, "first-2", 100);
    CHECK(chapterState(course, 0, scores).perfect);
    // A chapter of words only: passed once read
    CHECK_FALSE(chapterState(course, 2, scores).passed);
    recordCourseScore(scores, "words-only-read", 100);
    CHECK(chapterState(course, 2, scores).passed);
    CHECK(levelPercent(course, 1, scores) == 100);
    // Played in once, a chapter stays open, whatever comes before it
    CourseScores later;
    CHECK_FALSE(chapterOpen(course, 1, later));
    recordCourseScore(later, "second-1", 40);
    CHECK(chapterOpen(course, 1, later));
    CHECK(levelPercent(course, 0, scores) == 50); // 100 and 0
    // Kept in a file
    const std::string path = (std::filesystem::temp_directory_path() / "lahn-course-scores-test.txt").string();
    REQUIRE(saveCourseScores(path, scores, error));
    CHECK(loadCourseScores(path).best == scores.best);
    std::filesystem::remove(path);
}
