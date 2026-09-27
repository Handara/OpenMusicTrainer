#include "doctest/doctest.h"

#include "core/lesson.h"

#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

static const char* CHART = "version 2\nresolution 480\nend 1920\ntempo 0 120\ntrack guitar Lead\ntuning 40 45 50 55 59 64\nn 0 0 0\n";

// A lesson folder in the temp directory: its lesson.lesson, plus stand-in media (the loader only checks they exist)
static std::string lessonFolder(const std::string& name, const std::string& lesson){
    fs::path folder = fs::temp_directory_path() / "omt_tests" / "lessons" / name;
    fs::remove_all(folder);
    fs::create_directories(folder);
    std::ofstream(folder / LESSON_FILE_NAME, std::ios::binary) << lesson;
    for (const char* media : {"em.png", "photo.JPG", "strum.wav", "hand.mpg"}) std::ofstream(folder / media) << "x";
    std::ofstream(folder / "riff.chart") << CHART;
    std::ofstream(folder / "broken.chart") << "version 1\n";
    return folder.string();
}

static const std::string HEADER = "version 1\ntitle First chords\n";

TEST_CASE("a full lesson loads"){
    std::string folder = lessonFolder("full",
        "# OpenMusicTrainer lesson\r\n" + HEADER + "category Guitar basics\nauthor Someone\ndescription Two chords.\n"
        "\nstep text\ntitle What is a chord?\ntext Three notes or more.\ntext A second paragraph.\n"
        "step image\nfile em.png\ncaption E minor\n"
        "step image\nfile photo.JPG\n"
        "step audio\nfile strum.wav\n"
        "step video\ntitle Slow motion\nfile hand.mpg\n"
        "step exercise\nexercise e-minor-open\ngoal 2\n"
        "step play\nfile riff.chart\ngoal 90%\n");
    Lesson lesson;
    std::string error;
    REQUIRE_MESSAGE(loadLesson(folder, lesson, error), error);
    CHECK(lesson.title == "First chords");
    CHECK(lesson.category == "Guitar basics");
    CHECK(lesson.author == "Someone");
    REQUIRE(lesson.steps.size() == 7);
    CHECK(lesson.steps[0].type == LessonStepType::Text);
    CHECK(lesson.steps[0].title == "What is a chord?");
    CHECK(lesson.steps[0].paragraphs == std::vector<std::string>{"Three notes or more.", "A second paragraph."});
    CHECK(lesson.steps[1].file == "em.png");
    CHECK(lesson.steps[1].caption == "E minor");
    CHECK(lesson.steps[4].type == LessonStepType::Video);
    CHECK(lesson.steps[5].exercise == "e-minor-open");
    CHECK(lesson.steps[5].goal == 2);
    CHECK(lesson.steps[6].goal == 90);
}

TEST_CASE("broken lessons are rejected with a clear message"){
    struct Case { const char* name; std::string content; const char* expected; };
    const Case cases[] = {
        {"no version",       "title T\nstep text\ntext Hi\n",                     "missing 'version'"},
        {"newer version",    "version 2\ntitle T\nstep text\ntext Hi\n",          "newer than this build supports"},
        {"no title",         "version 1\nstep text\ntext Hi\n",                   "missing 'title'"},
        {"no steps",         HEADER,                                               "at least one step"},
        {"unknown step",     HEADER + "step quiz\n",                               "unknown step type 'quiz'"},
        {"wrong key",        HEADER + "step text\nfile em.png\n",                  "'file' doesn't belong in a text step"},
        {"wrong key 2",      HEADER + "step image\nexercise x\n",                  "'exercise' doesn't belong in an image step"},
        {"header typo",      HEADER + "auther Me\nstep text\ntext Hi\n",           "unknown setting 'auther' before the first step"},
        {"empty text step",  HEADER + "step text\n",                               "needs a title or some text"},
        {"no file",          HEADER + "step image\ncaption Hi\n",                  "an image step needs 'file <name>'"},
        {"wrong type",       HEADER + "step audio\nfile em.png\n",                 "'em.png' isn't an audio file (.wav, .flac, .mp3)"},
        {"missing file",     HEADER + "step image\nfile gone.png\n",               "'gone.png' isn't in the lesson's folder"},
        {"outside folder",   HEADER + "step image\nfile ../em.png\n",              "must be a file in the lesson's folder"},
        {"no exercise",      HEADER + "step exercise\ngoal 2\n",                   "needs 'exercise <file name>'"},
        {"bad goal",         HEADER + "step exercise\nexercise x\ngoal two\n",     "goal is a count"},
        {"percent too big",  HEADER + "step play\nfile riff.chart\ngoal 120%\n",   "goal is a percentage"},
        {"broken chart",     HEADER + "step play\nfile broken.chart\n",            "broken.chart"},
        {"empty value",      HEADER + "step text\ntext\n",                         "'text' needs a value"},
    };
    for (const Case& c : cases){
        SUBCASE(c.name){
            Lesson lesson;
            std::string error;
            CHECK_FALSE(loadLesson(lessonFolder("bad", c.content), lesson, error));
            CHECK_MESSAGE(error.find(c.expected) != std::string::npos, error);
        }
    }
    // Errors point at the step's own line
    Lesson lesson;
    std::string error;
    CHECK_FALSE(loadLesson(lessonFolder("line", HEADER + "step text\ntext Hi\n\nstep image\n"), lesson, error));
    CHECK(error.find(":6: ") != std::string::npos);
}

TEST_CASE("a lesson survives a save and load"){
    Lesson original;
    original.title = "Round trip";
    original.category = "Tests";
    original.author = "Me";
    LessonStep text;
    text.title = "Hello";
    text.paragraphs = {"One.", "Two\nand three."}; // typed with a line break: saved as two paragraphs
    LessonStep image;
    image.type = LessonStepType::Image;
    image.file = "em.png";
    image.caption = "A caption\nwith a line break";
    LessonStep play;
    play.type = LessonStepType::Play;
    play.file = "riff.chart";
    play.goal = 75;
    original.steps = {text, image, play};

    std::string folder = lessonFolder("roundtrip", ""), error;
    REQUIRE_MESSAGE(saveLesson(folder, original, error), error);
    Lesson loaded;
    REQUIRE_MESSAGE(loadLesson(folder, loaded, error), error);
    CHECK(loaded.title == "Round trip");
    CHECK(loaded.author == "Me");
    REQUIRE(loaded.steps.size() == 3);
    CHECK(loaded.steps[0].paragraphs == std::vector<std::string>{"One.", "Two", "and three."});
    CHECK(loaded.steps[1].caption == "A caption with a line break");
    CHECK(loaded.steps[2].goal == 75);
}

TEST_CASE("goals: the step's own, or the usual one"){
    LessonStep step;
    step.type = LessonStepType::Exercise;
    CHECK(lessonGoal(step, ExerciseType::Scale) == 1);     // a clean pass
    CHECK(lessonGoal(step, ExerciseType::Intervals) == 5); // right answers in a row
    step.goal = 3;
    CHECK(lessonGoal(step, ExerciseType::Scale) == 3);
    step = {};
    step.type = LessonStepType::Play;
    CHECK(lessonGoal(step) == 80);
}

TEST_CASE("scanning lessons and checking their exercises"){
    fs::path dir = fs::temp_directory_path() / "omt_tests" / "lessons";
    lessonFolder("uses-drill", HEADER + "category A\nstep exercise\nexercise drill\n");
    lessonFolder("uses-routine", HEADER + "category A\nstep exercise\nexercise daily\n");
    lessonFolder("uses-nothing", HEADER + "category A\nstep exercise\nexercise nope\n");
    std::vector<LessonEntry> lessons = scanLessons(dir.string(), false);
    CHECK(lessons.size() >= 3);

    ExerciseEntry drill;
    drill.name = "drill";
    drill.builtIn = true;
    drill.exercise.type = ExerciseType::Scale;
    ExerciseEntry routine = drill;
    routine.name = "daily";
    routine.exercise.type = ExerciseType::Routine;
    checkLessonExercises(lessons, {drill, routine});
    auto errorOf = [&](const std::string& id){
        for (const LessonEntry& entry : lessons) if (entry.id == id) return entry.error;
        return std::string("not found");
    };
    CHECK(errorOf("user-uses-drill").empty()); // the player's lesson may use a built-in exercise
    CHECK(errorOf("user-uses-routine").find("a routine can't be a lesson step") != std::string::npos);
    CHECK(errorOf("user-uses-nothing") == "uses-nothing/lesson.lesson: step 1 (nope): there's no nope.exercise");
    CHECK(errorOf("user-bad").find("bad/lesson.lesson:") == 0); // broken files show a short path
}

TEST_CASE("lesson progress: passed steps stay passed, and it survives a save and load"){
    LessonProgress progress;
    passStep(progress, 4);
    passStep(progress, 1);
    passStep(progress, 4); // twice: kept once
    CHECK(progress.passed == std::vector<int>{1, 4});
    CHECK(stepPassed(progress, 4));
    CHECK_FALSE(stepPassed(progress, 2));
    progress.reached = 5;
    progress.completed = true;

    std::string path = (fs::temp_directory_path() / "omt_tests" / "lesson-progress.txt").string(), error;
    REQUIRE(saveLessonProgress(path, progress, error));
    LessonProgress loaded = loadLessonProgress(path);
    CHECK(loaded.reached == 5);
    CHECK(loaded.passed == std::vector<int>{1, 4});
    CHECK(loaded.completed);
    CHECK(loadLessonProgress(path + ".missing").reached == 0);
}
