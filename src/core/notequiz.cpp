#include "core/notequiz.h"

#include "core/files.h"
#include "core/music.h"

#include <algorithm>
#include <fstream>
#include <sstream>

std::vector<NeckStep> noteQuizPrompts(const NoteQuizConfig& config, std::mt19937& random){
    std::vector<NeckStep> prompts;
    if (config.notes.empty()) return prompts;
    const int size = (int)config.notes.size();
    int last = -1;
    for (int i = 0; i < config.count; i++){
        int pick;
        if (config.inOrder) pick = i % size;
        else do pick = std::uniform_int_distribution<int>(0, size - 1)(random); while (size > 1 && pick == last);
        prompts.push_back(config.notes[pick]);
        last = pick;
    }
    return prompts;
}

void startNoteQuiz(NoteQuizRun& run, const std::vector<NeckStep>& prompts){
    run = NoteQuizRun{};
    run.prompts = prompts;
}

bool playNoteQuiz(NoteQuizRun& run, const NoteQuizConfig& config, int pitch){
    if (noteQuizDone(run) || pitch < 0) return false;
    const int asked = run.prompts[run.next].pitch;
    const bool right = config.anyOctave ? (pitch - asked) % 12 == 0 : pitch == asked;
    if (!right){
        run.slipped = true;
        run.mistakes++;
        run.lastWrong = pitch;
        return false;
    }
    run.firstTime.push_back(!run.slipped);
    run.slipped = false;
    run.next++;
    return true;
}

bool noteQuizDone(const NoteQuizRun& run){
    return run.next >= run.prompts.size();
}

int noteQuizRight(const NoteQuizRun& run){
    return (int)std::count(run.firstTime.begin(), run.firstTime.end(), true);
}

bool noteQuizPassed(const NoteQuizRun& run, const NoteQuizConfig& config){
    return noteQuizDone(run) && noteQuizRight(run) >= std::min(config.pass, (int)run.prompts.size());
}

bool placeNotes(const std::vector<int>& pitches, const std::vector<int>& tuning, const std::vector<int>& strings,
                std::vector<NeckStep>& out, std::string& error){
    out.clear();
    for (int pitch : pitches){
        NeckStep best{ pitch, -1, -1 };
        for (int string = 0; string < (int)tuning.size(); string++){
            if (!strings.empty() && std::find(strings.begin(), strings.end(), string) == strings.end()) continue;
            const int fret = pitch - tuning[string];
            if (fret < 0 || fret > 24) continue;
            if (best.string < 0 || fret < best.fret) best = { pitch, string, fret };
        }
        if (best.string < 0){
            error = std::string(pitchClassName(pitch)) + std::to_string(pitchOctave(pitch)) + " isn't on the strings allowed";
            return false;
        }
        out.push_back(best);
    }
    return true;
}

// "1st", "2nd", "3rd", "4th"... "11th", "12th", "13th", "21st"
static std::string ordinal(int n){
    const int last = n % 10, lastTwo = n % 100;
    const char* suffix = (lastTwo >= 11 && lastTwo <= 13) ? "th" : last == 1 ? "st" : last == 2 ? "nd" : last == 3 ? "rd" : "th";
    return std::to_string(n) + suffix;
}

std::string notePlaceText(const NeckStep& note, const std::vector<int>& tuning){
    if (note.string < 0 || note.string >= (int)tuning.size()) return "";
    // A string by its note; two of the same name (a guitar's two Es), the low one and the high one
    std::string name = pitchClassName(tuning[note.string]);
    int same = 0;
    for (int pitch : tuning) if ((pitch - tuning[note.string]) % 12 == 0) same++;
    if (same > 1){
        const bool lowest = std::none_of(tuning.begin(), tuning.end(), [&](int pitch){ return (pitch - tuning[note.string]) % 12 == 0 && pitch < tuning[note.string]; });
        name = (lowest ? "low " : "high ") + name;
    }
    if (note.fret == 0) return "the open " + name + " string";
    return "the " + ordinal(note.fret) + " fret of the " + name + " string";
}

NoteQuizStats loadNoteQuizStats(const std::string& path){
    NoteQuizStats stats;
    std::ifstream in(path);
    std::string key;
    int value;
    while (in >> key >> value){
        if (key == "runs") stats.runs = value;
        else if (key == "passed") stats.passed = value;
    }
    return stats;
}

bool saveNoteQuizStats(const std::string& path, const NoteQuizStats& stats, std::string& error){
    std::ostringstream out;
    out << "runs " << stats.runs << "\npassed " << stats.passed << "\n";
    return writeFileAtomically(path, out.str(), error);
}
