#include "core/chart.h"

#include "core/files.h"

#include <algorithm>
#include <charconv>
#include <fstream>
#include <sstream>

const int SUPPORTED_CHART_VERSION = 2; // 2 added time and key signatures; version 1 files load as 4/4 in C major

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
        } else if (keyword == "audio"){
            std::getline(ss >> std::ws, out.audioFile);
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
        } else if (keyword == "time"){
            TimeSignatureChange time{};
            char slash = 0;
            if (!(ss >> time.tick >> time.beats >> slash >> time.beatUnit) || slash != '/') return lineError("expected: time <tick> <beats>/<beat unit>, like time 0 3/4");
            bool unitOk = time.beatUnit == 1 || time.beatUnit == 2 || time.beatUnit == 4 || time.beatUnit == 8 || time.beatUnit == 16 || time.beatUnit == 32;
            if (time.tick < 0 || time.beats < 1 || time.beats > 32 || !unitOk) return lineError("time needs tick >= 0, 1-32 beats, and a beat unit of 1, 2, 4, 8, 16 or 32");
            out.timeSignatures.push_back(time);
        } else if (keyword == "key"){
            KeyChange change{};
            std::string tonic, mode;
            if (!(ss >> change.tick >> tonic >> mode)) return lineError("expected: key <tick> <tonic> major|minor, like key 0 G major");
            if (change.tick < 0) return lineError("key tick must be >= 0");
            if (!parseKeySignature(tonic, mode, change.key)) return lineError("unknown key '" + tonic + " " + mode + "' (keys have at most 7 sharps or flats: G# major is Ab major)");
            out.keys.push_back(change);
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

    // Time and key signatures: optional, but when given they start at tick 0 and change only on bar lines
    if (out.timeSignatures.empty()) out.timeSignatures.push_back({0, 4, 4});
    if (out.keys.empty()) out.keys.push_back({0, KeySignature{}});
    std::sort(out.timeSignatures.begin(), out.timeSignatures.end(),
              [](const TimeSignatureChange& a, const TimeSignatureChange& b){ return a.tick < b.tick; });
    std::sort(out.keys.begin(), out.keys.end(), [](const KeyChange& a, const KeyChange& b){ return a.tick < b.tick; });
    if (out.timeSignatures[0].tick != 0) return chartError("needs a time signature at tick 0 (or none at all, for 4/4)");
    if (out.keys[0].tick != 0) return chartError("needs a key at tick 0 (or none at all, for C major)");
    for (size_t i = 0; i < out.timeSignatures.size(); i++){
        const TimeSignatureChange& time = out.timeSignatures[i];
        if ((out.resolution * 4) % time.beatUnit != 0){
            return chartError("resolution " + std::to_string(out.resolution) + " can't divide a beat of 1/" + std::to_string(time.beatUnit));
        }
        if (i == 0) continue;
        const TimeSignatureChange& before = out.timeSignatures[i-1];
        if (time.tick == before.tick) return chartError("two time signatures at tick " + std::to_string(time.tick));
        if ((time.tick - before.tick) % ticksPerBar(out, before) != 0){
            return chartError("time signature at tick " + std::to_string(time.tick) + " isn't on a bar line");
        }
    }
    std::vector<int> bars = barTicks(out);
    for (size_t i = 0; i < out.keys.size(); i++){
        int tick = out.keys[i].tick;
        if (i > 0 && tick == out.keys[i-1].tick) return chartError("two keys at tick " + std::to_string(tick));
        if (!std::binary_search(bars.begin(), bars.end(), tick)) return chartError("key at tick " + std::to_string(tick) + " isn't on a bar line");
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

// Shortest text that reads back as exactly the same double: 0.1 -> "0.1", 120.0 -> "120"
static std::string formatNumber(double value){
    char buffer[32];
    std::to_chars_result result = std::to_chars(buffer, buffer + sizeof(buffer), value);
    return std::string(buffer, result.ptr);
}

static const char* trackTypeName(InstrumentType type){
    switch (type){
        case InstrumentType::Guitar: return "guitar";
        case InstrumentType::Bass: return "bass";
    }
    return "guitar";
}

bool saveChart(const std::string& path, const Chart& chart, std::string& error){
    std::ostringstream out;
    out << "# lahn chart\n";
    out << "version " << SUPPORTED_CHART_VERSION << "\n";
    if (!chart.title.empty()) out << "title " << chart.title << "\n";
    if (!chart.artist.empty()) out << "artist " << chart.artist << "\n";
    if (!chart.audioFile.empty()) out << "audio " << chart.audioFile << "\n";
    out << "resolution " << chart.resolution << "\n";
    out << "offset " << formatNumber(chart.offset) << "\n";
    out << "end " << chart.endTick << "\n\n";

    out << "# tempo <tick> <bpm>\n";
    for (const TempoChange& tempo : chart.tempoMap) out << "tempo " << tempo.tick << " " << formatNumber(tempo.bpm) << "\n";
    out << "# time <tick> <beats>/<beat unit>, key <tick> <tonic> major|minor\n";
    for (const TimeSignatureChange& time : chart.timeSignatures) out << "time " << time.tick << " " << time.beats << "/" << time.beatUnit << "\n";
    for (const KeyChange& key : chart.keys) out << "key " << key.tick << " " << keySignatureName(key.key) << "\n";

    for (const FrettedTrack& track : chart.frettedTracks){
        out << "\ntrack " << trackTypeName(track.type) << " " << track.name << "\n";
        out << "tuning";
        for (int pitch : track.tuning) out << " " << pitch;
        out << "\n# n <tick> <string> <fret> [duration]\n";
        for (const FrettedNote& note : track.notes){
            out << "n " << note.tick << " " << note.stringIndex << " " << note.fret;
            if (note.duration > 0) out << " " << note.duration;
            out << "\n";
        }
    }

    return writeFileAtomically(path, out.str(), error);
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

int ticksPerBar(const Chart& chart, const TimeSignatureChange& time){
    return time.beats * chart.resolution * 4 / time.beatUnit; // resolution is per quarter note: a beat of 1/8 is half of it
}

const TimeSignatureChange& timeSignatureAt(const Chart& chart, int tick){
    // The last change at or before the tick. There's always one at tick 0.
    auto after = std::upper_bound(chart.timeSignatures.begin(), chart.timeSignatures.end(), tick,
                                  [](int t, const TimeSignatureChange& time){ return t < time.tick; });
    return *(after - 1);
}

// Both walk the time signatures section by section: each holds a whole number of bars (changes are on bar lines)
int barStartTick(const Chart& chart, int bar){
    for (size_t i = 0; i < chart.timeSignatures.size(); i++){
        const TimeSignatureChange& time = chart.timeSignatures[i];
        int length = ticksPerBar(chart, time);
        bool last = i + 1 == chart.timeSignatures.size();
        int barsInSection = last ? bar + 1 : (chart.timeSignatures[i+1].tick - time.tick) / length;
        if (bar < barsInSection) return time.tick + bar * length;
        bar -= barsInSection;
    }
    return 0; // unreachable: the last section never runs out
}

int barNumberAt(const Chart& chart, int tick){
    int bar = 0;
    for (size_t i = 0; i < chart.timeSignatures.size(); i++){
        const TimeSignatureChange& time = chart.timeSignatures[i];
        int length = ticksPerBar(chart, time);
        bool last = i + 1 == chart.timeSignatures.size();
        if (last || tick < chart.timeSignatures[i+1].tick) return bar + std::max(0, tick - time.tick) / length;
        bar += (chart.timeSignatures[i+1].tick - time.tick) / length;
    }
    return bar;
}

std::vector<int> barTicks(const Chart& chart){
    std::vector<int> bars;
    for (int bar = 0, tick = 0; (tick = barStartTick(chart, bar)) <= chart.endTick; bar++) bars.push_back(tick);
    return bars;
}
