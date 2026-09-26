#include "chart.h"

#include <algorithm>
#include <fstream>
#include <sstream>

const int SUPPORTED_CHART_VERSION = 1;
const int MAX_FRET = 24;

// True if only whitespace is left on the line. Clears a failed read first, so that
// leftover text after it (e.g. "abc" where a number was expected) is still detected.
static bool atLineEnd(std::istringstream& ss){
    ss.clear();
    ss >> std::ws;
    return ss.eof();
}

bool loadChart(const std::string& path, Chart& out, std::string& error){
    std::ifstream file(path);
    if (!file){
        error = path + ": could not open file";
        return false;
    }

    out = Chart{};
    FrettedTrack* track = nullptr; // track that tuning/n lines belong to; re-taken after each push_back
    std::string line;
    int lineNumber = 0;

    auto lineError = [&](const std::string& message){
        error = path + ":" + std::to_string(lineNumber) + ": " + message;
        return false;
    };
    auto chartError = [&](const std::string& message){
        error = path + ": " + message;
        return false;
    };

    while (std::getline(file, line)){
        lineNumber++;
        if (!line.empty() && line.back() == '\r') line.pop_back(); // file saved with Windows line endings

        std::istringstream ss(line);
        std::string keyword;
        if (!(ss >> keyword) || keyword[0] == '#') continue;

        if (keyword == "version"){
            if (!(ss >> out.version)) return lineError("expected: version <number>");
        } else if (keyword == "title"){
            std::getline(ss >> std::ws, out.title);
        } else if (keyword == "artist"){
            std::getline(ss >> std::ws, out.artist);
        } else if (keyword == "resolution"){
            if (!(ss >> out.resolution)) return lineError("expected: resolution <ticks per beat>");
        } else if (keyword == "offset"){
            if (!(ss >> out.offset)) return lineError("expected: offset <seconds>");
        } else if (keyword == "end"){
            if (!(ss >> out.endTick)) return lineError("expected: end <tick>");
        } else if (keyword == "tempo"){
            TempoChange tempo{};
            if (!(ss >> tempo.tick >> tempo.bpm)) return lineError("expected: tempo <tick> <bpm>");
            if (tempo.tick < 0 || tempo.bpm <= 0.0) return lineError("tempo needs tick >= 0 and bpm > 0");
            out.tempoMap.push_back(tempo);
        } else if (keyword == "track"){
            std::string typeName;
            FrettedTrack newTrack;
            ss >> typeName;
            if (typeName == "guitar") newTrack.type = InstrumentType::Guitar;
            else if (typeName == "bass") newTrack.type = InstrumentType::Bass;
            else return lineError("unknown track type '" + typeName + "'");
            std::getline(ss >> std::ws, newTrack.name);
            out.frettedTracks.push_back(newTrack);
            track = &out.frettedTracks.back();
        } else if (keyword == "tuning"){
            if (track == nullptr) return lineError("tuning before any track");
            if (!track->tuning.empty()) return lineError("track already has a tuning");
            int pitch;
            while (ss >> pitch){
                if (pitch < 0 || pitch > 127) return lineError("tuning pitch must be a MIDI note 0-127");
                track->tuning.push_back(pitch);
            }
            if (track->tuning.empty()) return lineError("expected: tuning <midi pitch per string, low to high>");
        } else if (keyword == "n"){
            if (track == nullptr || track->tuning.empty()) return lineError("note before its track's tuning");
            FrettedNote note{};
            if (!(ss >> note.tick >> note.stringIndex >> note.fret)){
                return lineError("expected: n <tick> <string> <fret> [duration]");
            }
            if (!(ss >> note.duration)) note.duration = 0; // optional field
            if (note.tick < 0) return lineError("note tick must be >= 0");
            if (note.stringIndex < 0 || note.stringIndex >= (int)track->tuning.size()){
                return lineError("string must be 0-" + std::to_string(track->tuning.size() - 1) + " for this tuning");
            }
            if (note.fret < 0 || note.fret > MAX_FRET) return lineError("fret must be 0-" + std::to_string(MAX_FRET));
            if (note.duration < 0) return lineError("duration must be >= 0");
            track->notes.push_back(note);
        } else {
            return lineError("unknown keyword '" + keyword + "'");
        }

        if (!atLineEnd(ss)) return lineError("unexpected text after '" + keyword + "'");
    }

    if (out.version == 0) return chartError("missing 'version'");
    if (out.version > SUPPORTED_CHART_VERSION){
        return chartError("chart format v" + std::to_string(out.version) + " is newer than this build supports (v"
                          + std::to_string(SUPPORTED_CHART_VERSION) + ")");
    }
    if (out.resolution <= 0) return chartError("missing or invalid 'resolution'");
    if (out.endTick <= 0) return chartError("missing or invalid 'end'");
    if (out.frettedTracks.empty()) return chartError("chart has no tracks");

    std::sort(out.tempoMap.begin(), out.tempoMap.end(),
              [](const TempoChange& a, const TempoChange& b){ return a.tick < b.tick; });
    if (out.tempoMap.empty() || out.tempoMap[0].tick != 0) return chartError("needs a tempo at tick 0");
    for (size_t i = 1; i < out.tempoMap.size(); i++){
        if (out.tempoMap[i].tick == out.tempoMap[i-1].tick){
            return chartError("two tempo changes at tick " + std::to_string(out.tempoMap[i].tick));
        }
    }

    for (FrettedTrack& t : out.frettedTracks){
        if (t.tuning.empty()) return chartError("track '" + t.name + "' has no tuning");

        // Order by tick, then string, so chords always come out in the same order
        std::sort(t.notes.begin(), t.notes.end(), [](const FrettedNote& a, const FrettedNote& b){
            if (a.tick != b.tick) return a.tick < b.tick;
            return a.stringIndex < b.stringIndex;
        });
        for (size_t i = 1; i < t.notes.size(); i++){
            const FrettedNote& a = t.notes[i-1];
            const FrettedNote& b = t.notes[i];
            if (a.tick == b.tick && a.stringIndex == b.stringIndex){
                return chartError("track '" + t.name + "': two notes on string " + std::to_string(b.stringIndex)
                                  + " at tick " + std::to_string(b.tick));
            }
        }
        if (!t.notes.empty() && t.notes.back().tick > out.endTick){
            return chartError("track '" + t.name + "': note at tick " + std::to_string(t.notes.back().tick)
                              + " is after 'end'");
        }
    }
    return true;
}

// Walks the tempo map section by section, adding the duration of each section the tick passes through
double tickToSeconds(const Chart& chart, int tick){
    double seconds = chart.offset;
    for (size_t i = 0; i < chart.tempoMap.size(); i++){
        const TempoChange& tempo = chart.tempoMap[i];
        if (tick <= tempo.tick) break;

        int sectionEnd = tick;
        bool hasNext = i + 1 < chart.tempoMap.size();
        if (hasNext && chart.tempoMap[i+1].tick < tick) sectionEnd = chart.tempoMap[i+1].tick;

        double beats = (double)(sectionEnd - tempo.tick) / chart.resolution;
        seconds += beats * 60.0 / tempo.bpm;
    }
    return seconds;
}
