#include "core/journal.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace {

const char* const KIND_NAMES[(int)ActivityKind::Count] = { "drill", "notes", "song", "game", "chapter" };

// A value as it's written: no spaces (they part the fields), none lost (the title, last, keeps its own)
std::string token(const std::string& text){
    std::string out = text;
    for (char& c : out) if (c == ' ' || c == '\t' || c == '\n' || c == '\r') c = '_';
    return out;
}

} // namespace

std::string writeActivity(const Activity& a){
    std::ostringstream out;
    out << a.date << " " << (a.minute / 60 < 10 ? "0" : "") << a.minute / 60 << ":" << (a.minute % 60 < 10 ? "0" : "") << a.minute % 60
        << " " << KIND_NAMES[(int)a.kind];
    if (!a.id.empty()) out << " id=" << token(a.id);
    if (!a.instrument.empty()) out << " inst=" << token(a.instrument);
    char seconds[32];
    std::snprintf(seconds, sizeof seconds, "%.1f", a.seconds);
    out << " sec=" << seconds;
    if (a.total > 0) out << " right=" << a.right << " total=" << a.total;
    if (a.tempo > 0) out << " tempo=" << a.tempo;
    if (a.clean) out << " clean=1";
    if (a.challenge) out << " challenge=1";
    if (a.band) out << " band=1";
    if (!a.grade.empty()) out << " grade=" << token(a.grade);
    if (a.rounds > 0) out << " rounds=" << a.rounds;
    if (a.combo > 0) out << " combo=" << a.combo;
    if (a.pp > 0.0f){
        char pp[32];
        std::snprintf(pp, sizeof pp, "%.1f", a.pp);
        out << " pp=" << pp;
    }
    if (a.unitDone) out << " unit=1";
    if (a.courseDone) out << " course=1";
    if (!a.notes.empty()){
        out << " notes=";
        for (size_t i = 0; i < a.notes.size(); i++) out << (i ? "," : "") << a.notes[i].pitch << ":" << a.notes[i].right << "/" << a.notes[i].asked;
    }
    if (!a.title.empty()) out << " title=" << a.title; // last: the rest of the line
    return out.str();
}

bool readActivity(const std::string& line, Activity& out){
    std::istringstream in(line);
    std::string date, time, kind;
    if (!(in >> date >> time >> kind) || date.size() != 10 || date[4] != '-' || time.size() != 5 || time[2] != ':') return false;
    Activity a;
    a.date = date;
    a.minute = std::atoi(time.substr(0, 2).c_str()) * 60 + std::atoi(time.substr(3, 2).c_str());
    int found = -1;
    for (int k = 0; k < (int)ActivityKind::Count; k++) if (kind == KIND_NAMES[k]) found = k;
    if (found < 0) return false; // a kind from a newer lahn: skipped
    a.kind = (ActivityKind)found;
    std::string field;
    while (in >> field){
        const size_t equals = field.find('=');
        if (equals == std::string::npos) continue;
        const std::string key = field.substr(0, equals), value = field.substr(equals + 1);
        if (key == "title"){
            std::string rest;
            std::getline(in, rest);
            a.title = value + rest;
            break;
        }
        if (key == "id") a.id = value;
        else if (key == "inst") a.instrument = value;
        else if (key == "sec") a.seconds = (float)std::atof(value.c_str());
        else if (key == "right") a.right = std::atoi(value.c_str());
        else if (key == "total") a.total = std::atoi(value.c_str());
        else if (key == "tempo") a.tempo = std::atoi(value.c_str());
        else if (key == "clean") a.clean = value == "1";
        else if (key == "challenge") a.challenge = value == "1";
        else if (key == "band") a.band = value == "1";
        else if (key == "grade") a.grade = value;
        else if (key == "rounds") a.rounds = std::atoi(value.c_str());
        else if (key == "combo") a.combo = std::atoi(value.c_str());
        else if (key == "pp") a.pp = std::max(0.0f, (float)std::atof(value.c_str()));
        else if (key == "unit") a.unitDone = value == "1";
        else if (key == "course") a.courseDone = value == "1";
        else if (key == "notes"){
            std::istringstream notes(value);
            std::string one;
            while (std::getline(notes, one, ',')){
                NoteTally tally;
                if (std::sscanf(one.c_str(), "%d:%d/%d", &tally.pitch, &tally.right, &tally.asked) == 3 && tally.asked > 0) a.notes.push_back(tally);
            }
        }
    }
    out = a;
    return true;
}

std::vector<Activity> loadJournal(const std::string& path){
    std::vector<Activity> activities;
    std::ifstream in(path);
    std::string line;
    while (std::getline(in, line)){
        if (!line.empty() && line.back() == '\r') line.pop_back();
        Activity activity;
        if (readActivity(line, activity)) activities.push_back(activity);
    }
    return activities;
}

bool appendToJournal(const std::string& path, const Activity& activity, std::string& error){
    std::ofstream out(path, std::ios::app);
    if (!out){
        error = "Couldn't write to " + path;
        return false;
    }
    out << writeActivity(activity) << "\n";
    return true;
}
