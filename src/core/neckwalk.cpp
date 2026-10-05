#include "core/neckwalk.h"

#include "core/files.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <sstream>

const int RIGHT_POINTS = 100;   // a walk note right, times the streak plus one
const int CLEARED_POINTS = 500; // a round all right, times the streak with it
const int FOURTH = 5;           // the next round's note: a fourth up

const NeckWalkLevel& neckWalkEasy(){
    static const NeckWalkLevel easy = { "Easy", 2, 3, 5, 2, 12, 0.25f, 5 };
    return easy;
}

std::vector<NeckStep> neckWalkSteps(int pitchClass, const std::vector<int>& tuning, int firstString, int count, int nearFret,
                                    int maxFret, int notes){
    std::vector<NeckStep> places; // the lowest string first
    int near = nearFret;
    for (int string = std::max(0, firstString); string < firstString + count && string < (int)tuning.size(); string++){
        int best = -1;
        for (int fret = 0; fret <= maxFret; fret++){
            if ((tuning[string] + fret) % 12 != pitchClass) continue;
            if (best < 0 || std::abs(fret - near) < std::abs(best - near)) best = fret;
        }
        if (best < 0) return {};
        places.push_back({ tuning[string] + best, string, best });
        near = best;
    }
    std::vector<NeckStep> walk;
    if (places.empty()) return walk;
    // From the highest string down, back up, and down again, until there are enough
    int at = (int)places.size() - 1, direction = -1;
    while ((int)walk.size() < notes){
        walk.push_back(places[at]);
        if (places.size() == 1) continue;
        if (at + direction < 0 || at + direction >= (int)places.size()) direction = -direction;
        at += direction;
    }
    return walk;
}

// A round on `root`: on some strings side by side, somewhere on the neck, chosen at random
static void startRound(NeckWalkGame& game, int round, int root){
    game.round = round;
    game.root = root;
    game.judged = false;
    const NeckWalkLevel& level = game.level;
    const int strings = (int)game.tuning.size();
    for (int attempt = 0; attempt < 50; attempt++){
        const int count = std::min(strings, std::uniform_int_distribution<int>(level.minStrings, level.maxStrings)(game.random));
        const int first = std::uniform_int_distribution<int>(0, strings - count)(game.random);
        const int near = std::uniform_int_distribution<int>(0, std::max(0, level.maxFret - 3))(game.random);
        game.walk = neckWalkSteps(root, game.tuning, first, count, near, level.maxFret, level.notes);
        if (!game.walk.empty()) break;
    }
    game.notes.assign(game.walk.size(), WalkNote::Due);
    game.heardWrong.assign(game.walk.size(), false);
}

void startNeckWalk(NeckWalkGame& game, const NeckWalkLevel& level, const std::vector<int>& tuning, unsigned seed,
                   double startTime, float tempo){
    game = NeckWalkGame{};
    game.level = level;
    game.tuning = tuning;
    game.random.seed(seed);
    game.startTime = startTime;
    game.beatSeconds = 60.0 / std::max(1.0f, tempo);
    game.lives = level.lives;
    if (tuning.empty()){
        game.over = true;
        return;
    }
    startRound(game, 0, std::uniform_int_distribution<int>(0, 11)(game.random));
}

double neckWalkRoundStart(const NeckWalkGame& game, int round){
    return game.startTime + (double)round * NECK_WALK_ROUND_BEATS * game.beatSeconds;
}

double neckWalkNoteTime(const NeckWalkGame& game, int note){
    return neckWalkRoundStart(game, game.round) + (NECK_WALK_BAR_BEATS + note * game.level.beatsPerNote) * game.beatSeconds;
}

double neckWalkVerdictTime(const NeckWalkGame& game){
    return neckWalkNoteTime(game, (int)game.walk.size() - 1) + game.level.beatsPerNote * game.beatSeconds;
}

NeckWalkEvents neckWalkPlayed(NeckWalkGame& game, int pitch, double time){
    NeckWalkEvents events;
    if (game.over || game.judged || pitch < 0) return events;
    int nearest = -1;
    for (int i = 0; i < (int)game.notes.size(); i++){
        if (game.notes[i] != WalkNote::Due || std::abs(time - neckWalkNoteTime(game, i)) > game.level.windowSeconds) continue;
        if (nearest < 0 || std::abs(time - neckWalkNoteTime(game, i)) < std::abs(time - neckWalkNoteTime(game, nearest))) nearest = i;
    }
    if (nearest < 0) return events;
    if (pitch % 12 != game.root){
        game.heardWrong[nearest] = true;
        return events;
    }
    game.notes[nearest] = WalkNote::Right;
    game.score += (long long)RIGHT_POINTS * (game.streak + 1);
    game.rightByNote[game.root]++;
    events.right = nearest;
    return events;
}

NeckWalkEvents neckWalkUpdate(NeckWalkGame& game, double time){
    NeckWalkEvents events;
    if (game.over) return events;
    for (int i = 0; i < (int)game.notes.size(); i++){
        if (game.notes[i] != WalkNote::Due || time <= neckWalkNoteTime(game, i) + game.level.windowSeconds) continue;
        game.notes[i] = game.heardWrong[i] ? WalkNote::Wrong : WalkNote::Missed;
        game.wrongByNote[game.root]++;
        (game.heardWrong[i] ? events.wrong : events.missed) = i;
    }
    if (!game.judged && time >= neckWalkVerdictTime(game)){
        game.judged = true;
        const bool clean = std::all_of(game.notes.begin(), game.notes.end(), [](WalkNote note){ return note == WalkNote::Right; });
        if (clean){
            game.streak++;
            game.bestStreak = std::max(game.bestStreak, game.streak);
            game.cleared++;
            game.score += (long long)CLEARED_POINTS * game.streak;
            events.cheer = true;
        } else {
            game.streak = 0;
            game.lives--;
            events.aww = true;
            if (game.lives <= 0){
                game.over = true;
                events.over = true;
                return events;
            }
        }
    }
    if (game.judged && time >= neckWalkRoundStart(game, game.round + 1)){
        startRound(game, game.round + 1, (game.root + FOURTH) % 12);
        events.newRound = true;
    }
    return events;
}

NeckWalkStats loadNeckWalkStats(const std::string& path){
    NeckWalkStats stats;
    std::ifstream in(path);
    std::string line;
    while (std::getline(in, line)){
        std::istringstream ss(line);
        std::string kind;
        ss >> kind;
        if (kind == "game"){
            NeckWalkRecord record;
            if (ss >> record.date >> record.score >> record.cleared >> record.bestStreak) stats.games.push_back(record);
        } else if (kind == "note"){
            int note, right, wrong;
            if (ss >> note >> right >> wrong && note >= 0 && note < 12){
                stats.right[note] = right;
                stats.wrong[note] = wrong;
            }
        }
    }
    return stats;
}

bool saveNeckWalkStats(const std::string& path, const NeckWalkStats& stats, std::string& error){
    std::ostringstream out;
    out << "# lahn neck walk: game <date> <score> <rounds cleared> <best streak>; note <pitch class> <right> <wrong>\n";
    for (const NeckWalkRecord& game : stats.games)
        out << "game " << game.date << " " << game.score << " " << game.cleared << " " << game.bestStreak << "\n";
    for (int note = 0; note < 12; note++)
        if (stats.right[note] || stats.wrong[note]) out << "note " << note << " " << stats.right[note] << " " << stats.wrong[note] << "\n";
    return writeFileAtomically(path, out.str(), error);
}

void addNeckWalkGame(NeckWalkStats& stats, const NeckWalkGame& game, const std::string& date){
    stats.games.push_back({ date, game.score, game.cleared, game.bestStreak });
    for (int note = 0; note < 12; note++){
        stats.right[note] += game.rightByNote[note];
        stats.wrong[note] += game.wrongByNote[note];
    }
}

long long neckWalkBest(const NeckWalkStats& stats){
    long long best = 0;
    for (const NeckWalkRecord& game : stats.games) best = std::max(best, game.score);
    return best;
}
