#include "core/ranking.h"

#include "core/files.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <sstream>

float runAccuracy(int perfect, int good, int miss){
    int total = perfect + good + miss;
    if (total == 0) return 0.0f;
    return 100.0f * (perfect * 3 + good) / (3.0f * total);
}

Grade gradeFor(float accuracy, int misses){
    if (accuracy >= 100.0f - 1e-4f) return Grade::SS;
    if (accuracy >= 95.0f && misses == 0) return Grade::S;
    if (accuracy >= 90.0f) return Grade::A;
    if (accuracy >= 80.0f) return Grade::B;
    if (accuracy >= 70.0f) return Grade::C;
    return Grade::D;
}

const char* gradeName(Grade grade){
    switch (grade){
        case Grade::SS: return "SS";
        case Grade::S:  return "S";
        case Grade::A:  return "A";
        case Grade::B:  return "B";
        case Grade::C:  return "C";
        case Grade::D:  return "D";
    }
    return "D";
}

TimingStats timingStats(const std::vector<float>& errorsMs){
    TimingStats stats;
    if (errorsMs.empty()) return stats;
    double sum = 0.0;
    for (float error : errorsMs) sum += error;
    stats.meanMs = (float)(sum / errorsMs.size());
    if (errorsMs.size() < 2) return stats;
    double squares = 0.0;
    for (float error : errorsMs) squares += (error - stats.meanMs) * (error - stats.meanMs);
    stats.unstableRate = (float)(std::sqrt(squares / errorsMs.size()) * 10.0);
    return stats;
}

// FNV-1a: a small, well-spread hash, plenty for telling charts apart (this isn't security)
static void hashInto(uint64_t& hash, long long value){
    for (int i = 0; i < 8; i++){
        hash ^= (uint64_t)((value >> (i * 8)) & 0xff);
        hash *= 1099511628211ULL;
    }
}

std::string partFingerprint(const Chart& chart, int part){
    uint64_t hash = 14695981039346656037ULL;
    hashInto(hash, chart.resolution);
    hashInto(hash, (long long)std::llround(chart.offset * 1000000.0));
    for (const TempoChange& tempo : chart.tempoMap){
        hashInto(hash, tempo.tick);
        hashInto(hash, (long long)std::llround(tempo.bpm * 1000.0));
    }
    if (part >= 0 && part < (int)chart.frettedTracks.size()){
        const FrettedTrack& track = chart.frettedTracks[part];
        for (int pitch : track.tuning) hashInto(hash, pitch);
        for (const FrettedNote& note : track.notes){
            hashInto(hash, note.tick);
            hashInto(hash, note.stringIndex);
            hashInto(hash, note.fret);
        }
    }
    char text[17];
    std::snprintf(text, sizeof(text), "%016llx", (unsigned long long)hash);
    return text;
}

std::string recordsPath(const std::string& recordsDir, const std::string& songId, int part, const std::string& fingerprint){
    return (std::filesystem::path(recordsDir) / (songId + "-part" + std::to_string(part + 1) + "-" + fingerprint + ".txt")).string();
}

int addRun(std::vector<RunRecord>& records, const RunRecord& run){
    // After every record with at least this score: an equal score doesn't push an older one down
    auto at = std::upper_bound(records.begin(), records.end(), run.score,
                               [](int score, const RunRecord& record){ return score > record.score; });
    int place = (int)(at - records.begin());
    if (place >= KEPT_RUNS) return -1;
    records.insert(at, run);
    if ((int)records.size() > KEPT_RUNS) records.resize(KEPT_RUNS);
    return place;
}

std::vector<RunRecord> loadRuns(const std::string& path){
    std::vector<RunRecord> records;
    std::ifstream file(path);
    std::string line;
    while (std::getline(file, line)){
        std::istringstream ss(line);
        std::string key;
        if (!(ss >> key) || key != "run") continue; // comments, the version line, a newer version's keys
        RunRecord run;
        int instrument = 0;
        if (!(ss >> run.score >> run.accuracy >> run.maxCombo >> run.perfect >> run.good >> run.miss >> run.unstableRate
                 >> instrument >> run.date)) continue;
        run.withInstrument = instrument != 0;
        addRun(records, run); // sorted and trimmed, whatever order the file is in
    }
    return records;
}

bool saveRuns(const std::string& path, const std::vector<RunRecord>& records, std::string& error){
    std::ostringstream out;
    out << "# lahn records: the best runs of one part of one song\n";
    out << "version 1\n";
    out << "# run <score> <accuracy> <max combo> <perfect> <good> <miss> <unstable rate> <instrument 0/1> <date>\n";
    for (const RunRecord& run : records){
        out << "run " << run.score << " " << run.accuracy << " " << run.maxCombo << " " << run.perfect << " " << run.good << " "
            << run.miss << " " << run.unstableRate << " " << (run.withInstrument ? 1 : 0) << " " << run.date << "\n";
    }
    return writeFileAtomically(path, out.str(), error);
}
