#include "core/exercisefile.h"

#include "core/chart.h"

#include <algorithm>
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

// Reads a drill's tempo rules (tempo, pass), shared by scale and rhythm drills. Returns false if `key` isn't one;
// a wrong value is reported through lineError while still returning true.
template <typename LineError>
static bool readTempoSetting(const std::string& key, std::istringstream& ss, DrillTempo& tempo, LineError& lineError){
    if (key == "tempo"){
        if (!(ss >> tempo.startTempo >> tempo.maxTempo >> tempo.tempoStep) || tempo.startTempo < 20 || tempo.maxTempo > 400
            || tempo.startTempo > tempo.maxTempo || tempo.tempoStep < 1){
            lineError("expected: tempo <start> <goal> <step>, with 20 <= start <= goal <= 400");
        }
        return true;
    }
    if (key == "pass"){
        if (!(ss >> tempo.passPercent) || tempo.passPercent < 1 || tempo.passPercent > 100) lineError("pass must be a percentage, 1 to 100");
        return true;
    }
    return false;
}

// Reads a setting that rhythm and reading drills share: cells, bars, time, tuning, tempo, pass. Returns false if
// `key` isn't one; a wrong value is reported through lineError while still returning true.
template <typename Config, typename LineError>
static bool readRhythmSetting(const std::string& key, std::istringstream& ss, Config& config, LineError& lineError){
    if (key == "cells"){
        config.cells.clear();
        std::string name;
        while (ss >> name){
            if (!findRhythmCell(name)){
                lineError("unknown cell '" + name + "' (known: quarter, rest, eighths, offbeat, triplets, sixteenths, gallop, "
                          "reverse_gallop, dotted)");
                return true;
            }
            config.cells.push_back(name);
        }
        if (config.cells.empty()) lineError("expected: cells <names>");
    } else if (key == "bars"){
        if (!(ss >> config.bars) || config.bars < 1 || config.bars > 8) lineError("bars must be 1 to 8");
    } else if (key == "time"){
        if (!(ss >> config.beatsPerBar) || config.beatsPerBar < 2 || config.beatsPerBar > 7) lineError("time must be 2 to 7 beats a bar");
    } else if (key == "tuning"){
        std::vector<int> tuning;
        int pitch;
        while (ss >> pitch){
            if (pitch < 0 || pitch > 127){ lineError("tuning notes are MIDI numbers 0-127"); return true; }
            tuning.push_back(pitch);
        }
        if (tuning.empty()) lineError("expected: tuning <MIDI notes, lowest string first>");
        else config.tuning = tuning;
    } else {
        return readTempoSetting(key, ss, config.tempo, lineError);
    }
    return true;
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
    } else if (readTempoSetting(key, ss, drill.tempo, lineError)){
        // tempo or pass
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
            else if (line.rest == "routine") out.type = ExerciseType::Routine;
            else if (line.rest == "fretboard") out.type = ExerciseType::Fretboard;
            else if (line.rest == "rhythm") out.type = ExerciseType::Rhythm;
            else if (line.rest == "reading") out.type = ExerciseType::Reading;
            else return lineError("unknown exercise type '" + line.rest + "' (known: intervals, scale, routine, fretboard, rhythm, reading)");
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
    std::vector<int> fretboardStrings; // as written (1 = lowest): checked against the tuning once it's all read
    int fretboardStringsLine = 0;
    std::vector<int> readingStrings;   // the same, for reading drills
    int readingStringsLine = 0;
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
        } else if (out.type == ExerciseType::Routine && key == "step"){
            RoutineStep step;
            if (!(ss >> step.exercise >> step.minutes)) return lineError("expected: step <exercise file name> <minutes>");
            if (!(step.minutes > 0.0f && step.minutes <= 60.0f)) return lineError("a step lasts more than 0 and at most 60 minutes");
            out.routine.push_back(step);
        } else if (out.type == ExerciseType::Fretboard && key == "strings"){
            fretboardStrings.clear();
            int string;
            while (ss >> string) fretboardStrings.push_back(string);
            if (fretboardStrings.empty()) return lineError("expected: strings <string numbers, 1 = the lowest>");
            fretboardStringsLine = lineNumber;
        } else if (out.type == ExerciseType::Fretboard && key == "frets"){
            FretboardConfig& fretboard = out.fretboard;
            if (!(ss >> fretboard.lowestFret >> fretboard.highestFret)) return lineError("expected: frets <lowest> <highest>");
            if (fretboard.lowestFret < 0 || fretboard.highestFret > MAX_FRET || fretboard.lowestFret > fretboard.highestFret){
                return lineError("frets must be 0 to " + std::to_string(MAX_FRET) + ", lowest first");
            }
        } else if (out.type == ExerciseType::Fretboard && key == "notes"){
            std::string notes;
            ss >> notes;
            if (notes == "naturals") out.fretboard.naturalsOnly = true;
            else if (notes == "all") out.fretboard.naturalsOnly = false;
            else return lineError("notes must be naturals or all");
        } else if (out.type == ExerciseType::Fretboard && key == "tuning"){
            std::vector<int> tuning;
            int pitch;
            while (ss >> pitch){
                if (pitch < 0 || pitch > 127) return lineError("tuning notes are MIDI numbers 0-127");
                tuning.push_back(pitch);
            }
            if (tuning.empty() || tuning.size() > 12) return lineError("expected: tuning <1 to 12 MIDI notes, lowest string first>");
            out.fretboard.tuning = tuning;
        } else if (out.type == ExerciseType::Rhythm && readRhythmSetting(key, ss, out.rhythm, lineError)){
            if (!error.empty()) return false;
        } else if (out.type == ExerciseType::Reading && key == "key"){
            std::string word;
            if (!(ss >> word) || !parsePitchClass(word, out.reading.rootPitchClass)) return lineError("key must be a note name like G, F# or Bb");
        } else if (out.type == ExerciseType::Reading && key == "scale"){
            std::string word;
            if (!(ss >> word) || !findScale(word)) return lineError("unknown scale '" + word + "'");
            out.reading.scale = word;
        } else if (out.type == ExerciseType::Reading && key == "frets"){
            if (!(ss >> out.reading.lowestFret >> out.reading.highestFret)) return lineError("expected: frets <lowest> <highest>");
            if (out.reading.lowestFret < 0 || out.reading.highestFret > MAX_FRET || out.reading.lowestFret > out.reading.highestFret){
                return lineError("frets must be 0 to " + std::to_string(MAX_FRET) + ", lowest first");
            }
        } else if (out.type == ExerciseType::Reading && key == "strings"){
            readingStrings.clear();
            int string;
            while (ss >> string) readingStrings.push_back(string);
            if (readingStrings.empty()) return lineError("expected: strings <string numbers, 1 = the lowest>");
            readingStringsLine = lineNumber;
        } else if (out.type == ExerciseType::Reading && key == "leap"){
            if (!(ss >> out.reading.maxLeap) || out.reading.maxLeap < 1 || out.reading.maxLeap > 7) return lineError("leap must be 1 (by step) to 7");
        } else if (out.type == ExerciseType::Reading && readRhythmSetting(key, ss, out.reading, lineError)){
            if (!error.empty()) return false;
        } else if (out.type == ExerciseType::Scale && readDrillSetting(key, ss, out.drill, lineError)){
            if (!error.empty()) return false; // the setting was recognized but its value was wrong
        } else {
            return lineError("unknown setting '" + key + "' for this exercise type");
        }
        if (!atLineEnd(ss)) return lineError("unexpected text after '" + key + "'");
    }

    if (out.title.empty()) return fileError("missing 'title'");
    if (out.type == ExerciseType::Routine){
        if (out.routine.empty()) return fileError("a routine needs at least one 'step'");
        return true;
    }
    if (out.type == ExerciseType::Rhythm) return true;
    if (out.type == ExerciseType::Reading){
        for (int string : readingStrings){
            if (string < 1 || string > (int)out.reading.tuning.size()){
                lineNumber = readingStringsLine;
                return lineError("string " + std::to_string(string) + " doesn't exist: this tuning has "
                                 + std::to_string(out.reading.tuning.size()) + " strings");
            }
            out.reading.strings.push_back(string - 1);
        }
        // Built once now, so a position that can't be read from is reported here, where the author sees it
        std::mt19937 rng(0);
        std::vector<DrillNote> notes;
        std::string readingError;
        if (!buildReading(out.reading, rng, notes, readingError)) return fileError(readingError);
        return true;
    }
    if (out.type == ExerciseType::Fretboard){
        for (int string : fretboardStrings){
            if (string < 1 || string > (int)out.fretboard.tuning.size()){
                lineNumber = fretboardStringsLine;
                return lineError("string " + std::to_string(string) + " doesn't exist: this tuning has "
                                 + std::to_string(out.fretboard.tuning.size()) + " strings");
            }
            out.fretboard.strings.push_back(string - 1);
        }
        return true;
    }
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
        entry.name = file.path().stem().string();
        entry.id = std::string(builtIn ? "builtin-" : "user-") + entry.name;
        if (!loadExerciseFile(entry.path, entry.exercise, entry.error)){
            entry.exercise.title = entry.name;
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

const ExerciseEntry* findExercise(const std::vector<ExerciseEntry>& entries, bool fromBuiltIn, const std::string& name){
    auto find = [&](bool builtIn) -> const ExerciseEntry* {
        for (const ExerciseEntry& entry : entries) if (entry.builtIn == builtIn && entry.name == name) return &entry;
        return nullptr;
    };
    if (const ExerciseEntry* own = find(fromBuiltIn)) return own; // the asker's own folder first
    // Then, for the player's own, the built-in exercises. Built-in content never depends on what a player installed.
    return fromBuiltIn ? nullptr : find(true);
}

void checkRoutines(std::vector<ExerciseEntry>& entries){
    for (ExerciseEntry& entry : entries){
        if (entry.exercise.type != ExerciseType::Routine || !entry.error.empty()) continue;
        std::string fileName = entry.name + ".exercise";
        for (const RoutineStep& step : entry.exercise.routine){
            const ExerciseEntry* found = findExercise(entries, entry.builtIn, step.exercise);
            if (!found) entry.error = fileName + ": step '" + step.exercise + "': there's no " + step.exercise + ".exercise";
            else if (!found->error.empty()) entry.error = fileName + ": step '" + step.exercise + "' has an error of its own";
            else if (found->exercise.type == ExerciseType::Routine) entry.error = fileName + ": step '" + step.exercise + "' is a routine: routines can't contain routines";
            if (!entry.error.empty()) break;
        }
    }
}
