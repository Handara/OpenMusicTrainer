#include "doctest/doctest.h"

#include "core/intervals.h"

#include <filesystem>
#include <set>

TEST_CASE("interval names"){
    CHECK(std::string(intervalInfo(7).name) == "Perfect 5th");
    CHECK(std::string(intervalInfo(4).shortName) == "M3");
    CHECK(std::string(intervalInfo(12).name) == "Octave");
}

TEST_CASE("a new trainer only asks the starting intervals, in range, in the chosen direction"){
    IntervalTrainer trainer;
    trainer.rng.seed(1);
    std::set<int> asked;
    for (int i = 0; i < 200; i++){
        IntervalQuestion q = nextIntervalQuestion(trainer);
        asked.insert(q.semitones);
        CHECK(q.secondPitch - q.firstPitch == q.semitones); // ascending: second note higher
        CHECK(q.firstPitch >= 48);
        CHECK(q.secondPitch <= 67 + 12);
    }
    CHECK(asked == std::set<int>{7, 4}); // P5 and M3

    trainer.config.direction = IntervalDirection::Descending;
    IntervalQuestion down = nextIntervalQuestion(trainer);
    CHECK(down.firstPitch - down.secondPitch == down.semitones); // descending: second note lower
}

TEST_CASE("nine right out of the last ten unlocks the next interval"){
    IntervalTrainer trainer;
    trainer.rng.seed(2);
    int unlocked = 0;
    for (int i = 0; i < 10; i++){
        IntervalQuestion q = nextIntervalQuestion(trainer);
        bool answerRight = i != 3; // one mistake
        IntervalAnswerResult result = answerInterval(trainer, q, answerRight ? q.semitones : 1);
        if (i < 9) CHECK(result.unlocked == 0); // not before 10 answers
        else unlocked = result.unlocked;
    }
    CHECK(unlocked == 12); // the octave comes third
    CHECK(trainer.progress.unlockedCount == 3);
    CHECK(trainer.progress.recent.empty()); // the next unlock has to be earned from scratch
}

TEST_CASE("guessing doesn't unlock anything"){
    IntervalTrainer trainer;
    trainer.rng.seed(3);
    for (int i = 0; i < 100; i++){
        IntervalQuestion q = nextIntervalQuestion(trainer);
        answerInterval(trainer, q, i % 2 == 0 ? q.semitones : 1); // half right
    }
    CHECK(unlockedIntervals(trainer.config, trainer.progress).size() == 2);
}

TEST_CASE("weak intervals are asked more often"){
    IntervalTrainer trainer;
    trainer.rng.seed(4);
    trainer.progress.stats[7] = { 50, 50 }; // the 5th: always right
    trainer.progress.stats[4] = { 50, 10 }; // the 3rd: mostly wrong
    int thirds = 0;
    for (int i = 0; i < 2000; i++) if (nextIntervalQuestion(trainer).semitones == 4) thirds++;
    CHECK(thirds > 1300); // weights ~4.1 vs ~1.1: about 79% of questions
}

TEST_CASE("streaks and stats"){
    IntervalTrainer trainer;
    trainer.rng.seed(5);
    for (int i = 0; i < 5; i++){ IntervalQuestion q = nextIntervalQuestion(trainer); answerInterval(trainer, q, q.semitones); }
    IntervalQuestion q = nextIntervalQuestion(trainer);
    answerInterval(trainer, q, q.semitones == 7 ? 4 : 7); // wrong
    CHECK(trainer.streak == 0);
    CHECK(trainer.progress.bestStreak == 5);
    CHECK(trainer.progress.stats[4].asked + trainer.progress.stats[7].asked == 6);
}

TEST_CASE("progress survives a save and load"){
    IntervalProgress original;
    original.unlockedCount = 5;
    original.bestStreak = 23;
    original.recent = { true, false, true, true };
    original.stats[7] = { 40, 37 };
    original.stats[5] = { 3, 1 };

    std::filesystem::path dir = std::filesystem::temp_directory_path() / "lahn_tests";
    std::filesystem::create_directories(dir);
    std::string path = (dir / "intervals.txt").string();
    std::string error;
    REQUIRE_MESSAGE(saveIntervalProgress(path, original, error), error);
    IntervalProgress loaded = loadIntervalProgress(path);
    CHECK(loaded.unlockedCount == 5);
    CHECK(loaded.bestStreak == 23);
    CHECK(loaded.recent == original.recent);
    CHECK(loaded.stats[7].asked == 40);
    CHECK(loaded.stats[7].correct == 37);
    CHECK(loaded.stats[5].correct == 1);
    CHECK(loaded.stats[4].asked == 0);

    IntervalProgress missing = loadIntervalProgress((dir / "no_such_file.txt").string());
    CHECK(missing.unlockedCount == 0); // a fresh start
}

TEST_CASE("an exercise's own rules: a small pool, a custom unlock rule and range"){
    IntervalTrainer trainer;
    trainer.rng.seed(6);
    trainer.config.pool = { 4, 3, 7 };     // M3 vs m3 first, then the 5th
    trainer.config.startCount = 2;
    trainer.config.unlockCorrect = 3;
    trainer.config.unlockWindow = 3;
    trainer.config.lowestRoot = trainer.config.highestRoot = 60;
    std::set<int> asked;
    for (int i = 0; i < 50; i++){
        IntervalQuestion q = nextIntervalQuestion(trainer);
        CHECK(q.firstPitch == 60);
        if (i < 2){ asked.insert(q.semitones); answerInterval(trainer, q, q.semitones); }
    }
    CHECK(unlockedIntervals(trainer.config, trainer.progress).size() == 2); // 2 right, the rule needs 3
    IntervalQuestion q = nextIntervalQuestion(trainer);
    CHECK(answerInterval(trainer, q, q.semitones).unlocked == 7);
    CHECK(unlockedIntervals(trainer.config, trainer.progress).size() == 3);
    q = nextIntervalQuestion(trainer);
    CHECK(answerInterval(trainer, q, q.semitones).unlocked == 0); // everything is already unlocked
}
