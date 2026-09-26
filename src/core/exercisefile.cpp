#include "core/exercisefile.h"

#include <algorithm>
#include <sstream>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

const int SUPPORTED_EXERCISE_VERSION = 1;

// True if only whitespace is left (see the same helper in chart.cpp)
static bool atLineEnd(std::istringstream& ss){
    ss.clear();
    ss >> std::ws;
    return ss.eof();
}

// Reads one setting of a scale drill. Returns false if `key` isn't a drill setting; a wrong value is reported
// through lineError (which sets the error) while still returning true.
template <typename LineError>
static bool readDrillSetting(const std::string& key, std::istringstream& ss, ScaleDrillConfig& drill, LineError& lineError){
    std::string word;
    if (key == "key"){
        if (!(ss >> word) || !parsePitchClass(word, drill.rootPitchClass)) lineError("key must be a note name like G, F# or Bb");
    } else if (key == "scale"){
        if (!(ss >> word) || !findScale(word)) lineError("unknown scale '" + word + "'");
        else drill.scale = word;
    } else if (key == "octaves"){
        if (!(ss >> drill.octaves) || drill.octaves < 1 || drill.octaves > 4) lineError("octaves must be 1 to 4");
    } else if (key == "fingering"){
        ss >> word;
        if (word == "position") drill.fingering = Fingering::Position;
        else if (word == "3nps") drill.fingering = Fingering::ThreeNotesPerString;
        else lineError("fingering must be position or 3nps");
    } else if (key == "position"){
        if (!(ss >> drill.position) || drill.position < 0 || drill.position > 20) lineError("position must be a fret, 0 to 20");
    } else if (key == "direction"){
        ss >> word;
        if (word == "up") drill.direction = DrillDirection::Up;
        else if (word == "down") drill.direction = DrillDirection::Down;
        else if (word == "up_down") drill.direction = DrillDirection::UpDown;
        else lineError("direction must be up, down or up_down");
    } else if (key == "notes_per_beat"){
        if (!(ss >> drill.notesPerBeat) || drill.notesPerBeat < 1 || drill.notesPerBeat > 4) lineError("notes_per_beat must be 1 to 4");
    } else if (key == "tempo"){
        if (!(ss >> drill.startTempo >> drill.maxTempo >> drill.tempoStep) || drill.startTempo < 20 || drill.maxTempo > 400
            || drill.startTempo > drill.maxTempo || drill.tempoStep < 1){
            lineError("expected: tempo <start> <goal> <step>, with 20 <= start <= goal <= 400");
        }
    } else if (key == "pass"){
        if (!(ss >> drill.passPercent) || drill.passPercent < 1 || drill.passPercent > 100) lineError("pass must be a percentage, 1 to 100");
    } else if (key == "tuning"){
        drill.tuning.clear();
        int pitch;
        while (ss >> pitch){
            if (pitch < 0 || pitch > 127){ lineError("tuning pitches are MIDI notes 0-127"); return true; }
            drill.tuning.push_back(pitch);
        }
        if (drill.tuning.empty()) lineError("expected: tuning <MIDI pitch per string, lowest first>");
    } else {
        return false;
    }
    return true;
}

bool loadExerciseFile(const std::string& path, ExerciseFile& out, std::string& error){
    std::ifstream file(path);
    if (!file){
        error = path + ": could not open file";
        return false;
    }

    // First pass: collect the lines. The type decides which settings are allowed, and it
    // doesn't have to come first in the file, so settings are checked once everything is read.
    struct Line { int number; std::string key; std::string rest; };
    std::vector<Line> lines;
    std::string text;
    for (int number = 1; std::getline(file, text); number++){
        if (!text.empty() && text.back() == '\r') text.pop_back();
        std::istringstream ss(text);
        std::string key;
        if (!(ss >> key) || key[0] == '#') continue;
        std::string rest;
        std::getline(ss >> std::ws, rest);
        lines.push_back({number, key, rest});
    }

    out = ExerciseFile{};
    int lineNumber = 0;
    auto lineError = [&](const std::string& message){
        error = path + ":" + std::to_string(lineNumber) + ": " + message;
        return false;
    };
    auto fileError = [&](const std::string& message){
        error = path + ": " + message;
        return false;
    };

    int version = 0;
    bool hasType = false;
    for (const Line& line : lines){
        lineNumber = line.number;
        if (line.key == "version"){
            std::istringstream ss(line.rest);
            if (!(ss >> version) || !atLineEnd(ss)) return lineError("expected: version <number>");
        } else if (line.key == "type"){
            if (line.rest == "intervals") out.type = ExerciseType::Intervals;
            else if (line.rest == "scale") out.type = ExerciseType::Scale;
            else return lineError("unknown exercise type '" + line.rest + "' (known: intervals, scale)");
            hasType = true;
        }
    }
    if (version == 0) return fileError("missing 'version'");
    if (version > SUPPORTED_EXERCISE_VERSION){
        return fileError("exercise format v" + std::to_string(version) + " is newer than this build supports (v"
                         + std::to_string(SUPPORTED_EXERCISE_VERSION) + ")");
    }
    if (!hasType) return fileError("missing 'type'");

    IntervalConfig& config = out.intervals;
    bool poolSet = false, startSet = false;
    for (const Line& line : lines){
        lineNumber = line.number;
        std::istringstream ss(line.rest);
        const std::string& key = line.key;

        // Already read, or text that takes the whole rest of the line (so nothing can be left over)
        if (key == "version" || key == "type") continue;
        if (key == "title"){ out.title = line.rest; continue; }
        if (key == "category"){ if (!line.rest.empty()) out.category = line.rest; continue; }
        if (key == "author"){ out.author = line.rest; continue; }
        if (key == "description"){ out.description = line.rest; continue; }

        if (out.type == ExerciseType::Intervals && key == "direction"){
            std::string direction;
            ss >> direction;
            if (direction == "up") config.direction = IntervalDirection::Ascending;
            else if (direction == "down") config.direction = IntervalDirection::Descending;
            else if (direction == "together") config.direction = IntervalDirection::Harmonic;
            else return lineError("direction must be up, down or together");
        } else if (out.type == ExerciseType::Intervals && key == "intervals"){
            config.pool.clear();
            int semitones;
            while (ss >> semitones){
                if (semitones < 1 || semitones > INTERVAL_COUNT) return lineError("intervals are 1 (minor 2nd) to 12 (octave) semitones");
                if (std::count(config.pool.begin(), config.pool.end(), semitones)) return lineError("interval " + std::to_string(semitones) + " is listed twice");
                config.pool.push_back(semitones);
            }
            if (config.pool.size() < 2) return lineError("an exercise needs at least 2 intervals to choose between");
            poolSet = true;
        } else if (out.type == ExerciseType::Intervals && key == "start"){
            if (!(ss >> config.startCount) || config.startCount < 1) return lineError("expected: start <how many intervals are unlocked at first>");
            startSet = true;
        } else if (out.type == ExerciseType::Intervals && key == "unlock"){
            if (!(ss >> config.unlockCorrect >> config.unlockWindow)) return lineError("expected: unlock <right answers> <out of the last how many>");
            if (config.unlockWindow < 1 || config.unlockWindow > MAX_UNLOCK_WINDOW || config.unlockCorrect < 1 || config.unlockCorrect > config.unlockWindow){
                return lineError("unlock needs 1 <= right answers <= out of <= " + std::to_string(MAX_UNLOCK_WINDOW));
            }
        } else if (out.type == ExerciseType::Intervals && key == "range"){
            if (!(ss >> config.lowestRoot >> config.highestRoot)) return lineError("expected: range <lowest note> <highest note> (MIDI numbers)");
            if (config.lowestRoot < 0 || config.highestRoot > 127 || config.lowestRoot > config.highestRoot){
                return lineError("range must be two MIDI notes 0-127, lowest first");
            }
        } else if (out.type == ExerciseType::Intervals && key == "gap"){
            if (!(ss >> config.gapSeconds) || config.gapSeconds < 0.1f || config.gapSeconds > 3.0f) return lineError("gap must be 0.1 to 3 seconds");
        } else if (out.type == ExerciseType::Scale && readDrillSetting(key, ss, out.drill, lineError)){
            if (!error.empty()) return false; // the setting was recognized but its value was wrong
        } else {
            return lineError("unknown setting '" + key + "' for this exercise type");
        }
        if (!atLineEnd(ss)) return lineError("unexpected text after '" + key + "'");
    }

    if (out.title.empty()) return fileError("missing 'title'");
    if (out.type == ExerciseType::Scale){
        // Build it once now, so a drill that can't be played is reported here, where the author sees it
        std::vector<DrillNote> notes;
        std::string drillError;
        if (!buildScaleDrill(out.drill, notes, drillError)) return fileError(drillError);
        return true;
    }
    if (!poolSet) config.pool = IntervalConfig{}.pool; // the full course
    if (!startSet) config.startCount = std::min(2, (int)config.pool.size());
    if (config.startCount > (int)config.pool.size()) return fileError("'start' is more than the number of intervals");
    int widest = *std::max_element(config.pool.begin(), config.pool.end());
    if (config.highestRoot + widest > 127) return fileError("range is too high: the top note would go past MIDI 127");
    return true;
}

std::vector<ExerciseEntry> scanExercises(const std::string& dir, bool builtIn){
    std::vector<ExerciseEntry> entries;
    std::error_code ec; // a missing folder just means no exercises
    for (const fs::directory_entry& file : fs::directory_iterator(dir, ec)){
        if (!file.is_regular_file() || file.path().extension() != ".exercise") continue;
        ExerciseEntry entry;
        entry.path = file.path().string();
        entry.builtIn = builtIn;
        entry.id = std::string(builtIn ? "builtin-" : "user-") + file.path().stem().string();
        if (!loadExerciseFile(entry.path, entry.exercise, entry.error)){
            entry.exercise.title = file.path().stem().string();
            // Menus show the error: the file name is enough there, the full path would take several lines
            if (entry.error.rfind(entry.path, 0) == 0) entry.error = file.path().filename().string() + entry.error.substr(entry.path.size());
        }
        entries.push_back(entry);
    }
    std::sort(entries.begin(), entries.end(), [](const ExerciseEntry& a, const ExerciseEntry& b){
        if (a.exercise.category != b.exercise.category) return a.exercise.category < b.exercise.category;
        return a.exercise.title < b.exercise.title;
    });
    return entries;
}
