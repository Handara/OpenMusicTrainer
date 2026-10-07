#include "core/routine.h"

#include "core/files.h"

#include <algorithm>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <sstream>

// Howard Hinnant's calendar algorithm. Its trick: the year is taken to start in March, so February, and a leap
// day, come last. Every month before it then has a fixed length, and the day of the year is one formula.
int daysFromDate(int year, int month, int day){
    year -= month <= 2;                                          // January and February belong to the previous year
    int era = (year >= 0 ? year : year - 399) / 400;             // 400-year cycles (146097 days each)
    int yearOfEra = year - era * 400;                            // 0 to 399
    int dayOfYear = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1; // counted from March 1st
    int dayOfEra = yearOfEra * 365 + yearOfEra / 4 - yearOfEra / 100 + dayOfYear; // plus a leap day every 4 years, not every 100
    return era * 146097 + dayOfEra - 719468;                     // 719468 days from 0000-03-01 to 1970-01-01
}

// The same algorithm, run backwards
void dateFromDays(int days, int& year, int& month, int& day){
    days += 719468;
    int era = (days >= 0 ? days : days - 146096) / 146097;
    int dayOfEra = days - era * 146097;
    int yearOfEra = (dayOfEra - dayOfEra / 1460 + dayOfEra / 36524 - dayOfEra / 146096) / 365;
    int dayOfYear = dayOfEra - (365 * yearOfEra + yearOfEra / 4 - yearOfEra / 100);
    int marchMonth = (5 * dayOfYear + 2) / 153;                  // 0 = March ... 11 = February
    day = dayOfYear - (153 * marchMonth + 2) / 5 + 1;
    month = marchMonth < 10 ? marchMonth + 3 : marchMonth - 9;
    year = yearOfEra + era * 400 + (month <= 2);
}

int today(){
    std::time_t now = std::time(nullptr);
    std::tm local{};
#if defined(_WIN32)
    localtime_s(&local, &now); // the thread-safe versions: std::localtime shares one result between all callers
#else
    localtime_r(&now, &local);
#endif
    return daysFromDate(local.tm_year + 1900, local.tm_mon + 1, local.tm_mday);
}

void finishRoutine(RoutineProgress& progress, int day){
    progress.completed++;
    if (progress.lastDay == day) return;                  // already counted today
    progress.streak = progress.lastDay == day - 1 ? progress.streak + 1 : 1;
    progress.bestStreak = std::max(progress.bestStreak, progress.streak);
    progress.lastDay = day;
}

int currentStreak(const RoutineProgress& progress, int day){
    // Yesterday still counts: the streak lives on until today is over
    return progress.lastDay == day || progress.lastDay == day - 1 ? progress.streak : 0;
}

bool doneOnDay(const RoutineProgress& progress, int day){
    return progress.lastDay == day;
}

RoutineProgress loadRoutineProgress(const std::string& path){
    RoutineProgress progress;
    std::ifstream file(path);
    std::string line;
    while (std::getline(file, line)){
        std::istringstream ss(line);
        std::string key;
        if (!(ss >> key) || key[0] == '#') continue;
        if (key == "last_day"){
            int year, month, day;
            char dash1, dash2;
            if (ss >> year >> dash1 >> month >> dash2 >> day && dash1 == '-' && dash2 == '-') progress.lastDay = daysFromDate(year, month, day);
            continue;
        }
        int value;
        if (!(ss >> value) || value < 0) continue;
        if (key == "completed") progress.completed = value;
        else if (key == "streak") progress.streak = value;
        else if (key == "best_streak") progress.bestStreak = value;
    }
    return progress;
}

bool saveRoutineProgress(const std::string& path, const RoutineProgress& progress, std::string& error){
    std::ostringstream out;
    out << "# hardthz progress: routine\n";
    out << "version 1\n";
    out << "completed " << progress.completed << "\n";
    if (progress.lastDay >= 0){
        int year, month, day;
        dateFromDays(progress.lastDay, year, month, day);
        char date[16];
        std::snprintf(date, sizeof date, "%04d-%02d-%02d", year, month, day); // a date people can read: 2026-09-26
        out << "last_day " << date << "\n";
    }
    out << "streak " << progress.streak << "\n";
    out << "best_streak " << progress.bestStreak << "\n";
    return writeFileAtomically(path, out.str(), error);
}
