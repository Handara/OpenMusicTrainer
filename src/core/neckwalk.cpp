#include "core/neckwalk.h"

#include "core/files.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <sstream>

const int CLEARED_POINTS = 5; // a round all right: this many notes' worth, times the streak with it
const int FOURTH = 5;         // the next round's note: a fourth up
const double VERDICT_BEFORE_END = 0.5; // beats: the crowd answers this long before the next round

const NeckWalkLevel& neckWalkLevel(int index){
    static const NeckWalkLevel LEVELS[NECK_WALK_LEVELS] = {
        // name, key, strings, notes, beats a note, highest fret, window, points
        { "Easy", "easy", 2, 3, 4, 2.0, 12, 0.25f, 100 },
        { "Normal", "normal", 3, 4, 6, 1.0, 12, 0.18f, 200 },
        { "Hard", "hard", 4, 6, 8, 1.0, 15, 0.12f, 300 },
    };
    return LEVELS[std::clamp(index, 0, NECK_WALK_LEVELS - 1)];
}

int neckWalkLevelIndex(const std::string& key){
    for (int i = 0; i < NECK_WALK_LEVELS; i++) if (key == neckWalkLevel(i).key) return i;
    return -1;
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

// A walk of `root`: on some strings side by side, somewhere on the neck, chosen at random
static std::vector<NeckStep> randomWalk(NeckWalkGame& game, int root){
    const NeckWalkLevel& level = game.level;
    const int strings = (int)game.tuning.size();
    std::vector<NeckStep> walk;
    for (int attempt = 0; attempt < 50 && walk.empty(); attempt++){
        const int count = std::min(strings, std::uniform_int_distribution<int>(level.minStrings, level.maxStrings)(game.random));
        const int first = std::uniform_int_distribution<int>(0, strings - count)(game.random);
        const int near = std::uniform_int_distribution<int>(0, std::max(0, level.maxFret - 3))(game.random);
        walk = neckWalkSteps(root, game.tuning, first, count, near, level.maxFret, level.notes);
    }
    return walk;
}

// The next round: the one prepared a round ahead, and the one after it prepared
static void nextRound(NeckWalkGame& game, int round){
    game.round = round;
    game.root = game.nextRoot;
    game.walk = game.nextWalk;
    game.judged = false;
    game.notes.assign(game.walk.size(), WalkNote::Due);
    game.heardWrong.assign(game.walk.size(), false);
    game.nextRoot = (game.root + FOURTH) % 12;
    game.nextWalk = randomWalk(game, game.nextRoot);
}

void startNeckWalk(NeckWalkGame& game, int levelIndex, const std::vector<int>& tuning, unsigned seed, double startTime,
                   float tempo){
    game = NeckWalkGame{};
    game.levelIndex = std::clamp(levelIndex, 0, NECK_WALK_LEVELS - 1);
    game.level = neckWalkLevel(game.levelIndex);
    game.tuning = tuning;
    game.random.seed(seed);
    game.startTime = startTime;
    game.tempo = std::max(1.0f, tempo);
    game.beatSeconds = 60.0 / game.tempo;
    game.lives = NECK_WALK_LIVES;
    if (tuning.empty()){
        game.over = true;
        return;
    }
    game.nextRoot = std::uniform_int_distribution<int>(0, 11)(game.random);
    game.nextWalk = randomWalk(game, game.nextRoot);
    nextRound(game, 0);
}

double neckWalkRoundStart(const NeckWalkGame& game, int round){
    return game.startTime + (NECK_WALK_INTRO_BEATS + (double)round * NECK_WALK_ROUND_BEATS) * game.beatSeconds;
}

double neckWalkShowTime(const NeckWalkGame& game, int note){
    return neckWalkRoundStart(game, game.round) + note * game.level.beatsPerNote * game.beatSeconds;
}

double neckWalkNoteTime(const NeckWalkGame& game, int note){
    return neckWalkShowTime(game, note) + NECK_WALK_PART_BEATS * game.beatSeconds;
}

double neckWalkVerdictTime(const NeckWalkGame& game){
    return neckWalkRoundStart(game, game.round + 1) - VERDICT_BEFORE_END * game.beatSeconds;
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
    game.score += (long long)game.level.points * (game.streak + 1);
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
            game.score += (long long)CLEARED_POINTS * game.level.points * game.streak;
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
        nextRound(game, game.round + 1);
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
            if (!(ss >> record.date >> record.score >> record.cleared >> record.bestStreak)) continue;
            std::string level;
            if (ss >> level && neckWalkLevelIndex(level) >= 0) record.level = level;
            ss >> record.bpm;
            stats.games.push_back(record);
        } else if (kind == "note"){
            int note, right, wrong;
            if (ss >> note >> right >> wrong && note >= 0 && note < 12){
                stats.right[note] = right;
                stats.wrong[note] = wrong;
            }
        } else if (kind == "choice"){
            std::string level;
            int bpm;
            if (ss >> level >> bpm && neckWalkLevelIndex(level) >= 0){
                stats.lastLevel = neckWalkLevelIndex(level);
                stats.lastBpm = bpm;
            }
        }
    }
    return stats;
}

bool saveNeckWalkStats(const std::string& path, const NeckWalkStats& stats, std::string& error){
    std::ostringstream out;
    out << "# lahn neck walk: game <date> <score> <rounds cleared> <best streak> <level> <bpm>; "
           "note <pitch class> <right> <wrong>; choice <level> <bpm>\n";
    for (const NeckWalkRecord& game : stats.games)
        out << "game " << game.date << " " << game.score << " " << game.cleared << " " << game.bestStreak << " " << game.level << " "
            << game.bpm << "\n";
    for (int note = 0; note < 12; note++)
        if (stats.right[note] || stats.wrong[note]) out << "note " << note << " " << stats.right[note] << " " << stats.wrong[note] << "\n";
    if (stats.lastLevel >= 0) out << "choice " << neckWalkLevel(stats.lastLevel).key << " " << stats.lastBpm << "\n";
    return writeFileAtomically(path, out.str(), error);
}

void addNeckWalkGame(NeckWalkStats& stats, const NeckWalkGame& game, const std::string& date){
    stats.games.push_back({ date, game.score, game.cleared, game.bestStreak, game.level.key, (int)std::lround(game.tempo) });
    for (int note = 0; note < 12; note++){
        stats.right[note] += game.rightByNote[note];
        stats.wrong[note] += game.wrongByNote[note];
    }
    stats.lastLevel = game.levelIndex;
    stats.lastBpm = (int)std::lround(game.tempo);
}

long long neckWalkBest(const NeckWalkStats& stats, int levelIndex){
    long long best = 0;
    for (const NeckWalkRecord& game : stats.games)
        if (game.level == neckWalkLevel(levelIndex).key) best = std::max(best, game.score);
    return best;
}
