#include "core/today.h"

#include "core/drill.h"
#include "core/routine.h"

#include <filesystem>

namespace fs = std::filesystem;

TodaySummary summarizeToday(const std::vector<ExerciseEntry>& exercises, const std::string& progressDir, int day){
    TodaySummary summary;
    fs::file_time_type newestDrill;
    for (const ExerciseEntry& entry : exercises){
        if (!entry.error.empty()) continue;
        std::string progressPath = (fs::path(progressDir) / (entry.id + ".txt")).string();

        if (entry.exercise.type == ExerciseType::Routine){
            RoutineProgress progress = loadRoutineProgress(progressPath);
            int streak = currentStreak(progress, day);
            // The first routine, unless a later one has a longer streak going
            if (!summary.hasRoutine || streak > summary.streakDays){
                summary.hasRoutine = true;
                summary.routineTitle = entry.exercise.title;
                summary.routineDoneToday = doneOnDay(progress, day);
                summary.streakDays = streak;
                summary.routineMinutes = 0.0f;
                for (const RoutineStep& step : entry.exercise.routine) summary.routineMinutes += step.minutes;
            }
        } else if (entry.exercise.type == ExerciseType::Scale){
            // Only drills that were practiced have a progress file; the newest file is the drill played last
            std::error_code ec;
            fs::file_time_type written = fs::last_write_time(progressPath, ec);
            if (ec || (summary.hasDrill && written <= newestDrill)) continue;
            newestDrill = written;
            summary.hasDrill = true;
            summary.drillTitle = entry.exercise.title;
            summary.drillTempo = drillTempo(entry.exercise.drill, loadDrillProgress(progressPath));
        }
    }
    return summary;
}
