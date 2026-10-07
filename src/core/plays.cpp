#include "core/plays.h"

#include "core/files.h"

#include <fstream>
#include <sstream>

std::map<std::string, PlayCount> loadPlays(const std::string& path){
    std::map<std::string, PlayCount> plays;
    std::ifstream in(path);
    std::string line;
    while (std::getline(in, line)){
        std::istringstream ss(line);
        std::string id;
        PlayCount count;
        if (line.empty() || line[0] == '#' || !(ss >> id >> count.times >> count.last)) continue;
        plays[id] = count;
    }
    return plays;
}

bool recordPlay(const std::string& path, const std::string& id, const std::string& date, std::string& error){
    std::map<std::string, PlayCount> plays = loadPlays(path);
    PlayCount& count = plays[id];
    count.times++;
    count.last = date;
    std::ostringstream out;
    out << "# hardthz: how many times each exercise and lesson was taken up, and when last: <id> <times> <date>\n";
    for (const auto& [name, played] : plays) out << name << " " << played.times << " " << played.last << "\n";
    return writeFileAtomically(path, out.str(), error);
}
