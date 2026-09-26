#include "core/drill.h"

#include "core/files.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>

bool buildScaleDrill(const ScaleDrillConfig& config, std::vector<DrillNote>& out, std::string& error){
    out.clear();
    const ScaleInfo* scale = findScale(config.scale);
    if (!scale){
        error = "unknown scale '" + config.scale + "'";
        return false;
    }
    if (config.tuning.empty() || config.octaves < 1 || config.notesPerBeat < 1){
        error = "a drill needs a tuning, at least 1 octave and 1 note per beat";
        return false;
    }

    // The root: its first place on the lowest string, at or above where the hand sits
    int lowestString = config.tuning[0];
    int rootFret = ((config.rootPitchClass - lowestString) % 12 + 12) % 12;
    int position = config.position >= 0 ? config.position : std::max(0, rootFret - 1);
    while (rootFret < position - 1) rootFret += 12;
    int root = lowestString + rootFret;

    std::vector<int> up = scalePitches(config.rootPitchClass, *scale, root, root + 12 * config.octaves);
    std::vector<FretPosition> places;
    if (!fingerPitches(up, config.tuning, config.fingering, position, places, error)) return false;

    // The order the notes are played in, as indices into `up`: up-and-down turns around without repeating the top
    std::vector<int> order;
    int n = (int)up.size();
    if (config.direction != DrillDirection::Down) for (int i = 0; i < n; i++) order.push_back(i);
    if (config.direction == DrillDirection::Down) for (int i = n - 1; i >= 0; i--) order.push_back(i);
    if (config.direction == DrillDirection::UpDown) for (int i = n - 2; i >= 0; i--) order.push_back(i);

    for (int i = 0; i < (int)order.size(); i++){
        int k = order[i];
        out.push_back({(double)i / config.notesPerBeat, places[k].stringIndex, places[k].fret, up[k]});
    }
    return true;
}

Chart drillChart(const ScaleDrillConfig& config, const std::vector<DrillNote>& notes){
    Chart chart{};
    chart.version = 2;
    chart.title = config.scale;
    chart.resolution = 480; // divides evenly into 2, 3 and 4 notes a beat
    chart.tempoMap = {{0, 60.0}};
    chart.timeSignatures = {{0, 4, 4}};
    const ScaleInfo* scale = findScale(config.scale);
    chart.keys = {{0, scale ? scaleKeySignature(config.rootPitchClass, *scale) : KeySignature{}}};
    FrettedTrack track;
    track.type = InstrumentType::Guitar;
    track.name = "Drill";
    track.tuning = config.tuning;
    for (const DrillNote& note : notes){
        track.notes.push_back({(int)std::lround(note.beat * chart.resolution), note.stringIndex, note.fret, 0});
    }
    chart.frettedTracks = {track};
    int lastTick = track.notes.empty() ? 0 : track.notes.back().tick;
    chart.endTick = barStartTick(chart, barNumberAt(chart, lastTick) + 1);
    return chart;
}

int drillTempo(const ScaleDrillConfig& config, const DrillProgress& progress){
    return progress.tempo > 0 ? progress.tempo : config.startTempo;
}

DrillPassOutcome finishDrillPass(const ScaleDrillConfig& config, DrillProgress& progress, int tempo, float accuracyPercent){
    DrillPassOutcome outcome;
    outcome.clean = accuracyPercent >= config.passPercent;
    outcome.newBest = outcome.clean && tempo > progress.bestCleanTempo;
    progress.passes++;
    if (outcome.clean){
        progress.cleanPasses++;
        progress.bestCleanTempo = std::max(progress.bestCleanTempo, tempo);
        progress.tempo = std::min(config.maxTempo, tempo + config.tempoStep);
    } else if (accuracyPercent < 50.0f){
        progress.tempo = std::max(config.startTempo, tempo - config.tempoStep);
    } else {
        progress.tempo = tempo;
    }
    outcome.nextTempo = progress.tempo;
    return outcome;
}

DrillProgress loadDrillProgress(const std::string& path){
    DrillProgress progress;
    std::ifstream file(path);
    std::string line;
    while (std::getline(file, line)){
        std::istringstream ss(line);
        std::string key;
        if (!(ss >> key) || key[0] == '#') continue;
        int value;
        if (!(ss >> value) || value < 0) continue;
        if (key == "tempo") progress.tempo = value;
        else if (key == "best_clean_tempo") progress.bestCleanTempo = value;
        else if (key == "passes") progress.passes = value;
        else if (key == "clean_passes") progress.cleanPasses = value;
    }
    return progress;
}

bool saveDrillProgress(const std::string& path, const DrillProgress& progress, std::string& error){
    std::ostringstream out;
    out << "# OpenMusicTrainer progress: scale drill\n";
    out << "version 1\n";
    out << "tempo " << progress.tempo << "\n";
    out << "best_clean_tempo " << progress.bestCleanTempo << "\n";
    out << "passes " << progress.passes << "\n";
    out << "clean_passes " << progress.cleanPasses << "\n";
    return writeFileAtomically(path, out.str(), error);
}
