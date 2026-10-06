#pragma once

#include "core/course.h"
#include "learn/exercise.h"

#include <memory>
#include <string>
#include <vector>

// A course's chapter (core/course): its few words to read beside its drills, each with its best score, passed or
// PERFECT. Enter goes on with the first drill not passed yet (or the one chosen); a drill's run scored, its best is
// kept, and once it's passed the next one starts by itself. Esc from a drill comes back here, from here to the level.
class CourseChapter : public Exercise {
public:
    // `drills`: each of the chapter's drills (core/course courseDrills), the exercise it runs
    CourseChapter(const Course& course, int lesson, std::vector<ExerciseEntry> drills, ExerciseFactory create,
                  const std::string& scoresPath);
    ~CourseChapter() override;
    void update() override;
    void draw() override;
    bool wantsToLeave() const override { return false; } // Esc (learnBack) ends it
    bool back() override;                                // a drill running: back to the chapter

private:
    void startDrill(int index);
    void stopDrill();
    void scored(int percent);
    int firstNotPassed() const;
    void drawRunningBar(float s);

    Course course;
    int lesson;
    std::vector<CourseDrill> drills;
    std::vector<ExerciseEntry> entries;
    ExerciseFactory create;
    std::string scoresPath;
    CourseScores scores;
    int chosen = 0;
    float scroll = 0.0f;
    int runningIndex = -1;
    std::unique_ptr<Exercise> running;
    double passedAt = -1.0;     // the running drill just passed: on to the next a moment later
    double chapterPassedAt = -100.0;
    int lastPercent = -1;       // the running drill's last run
    bool lastWasBest = false;
};
