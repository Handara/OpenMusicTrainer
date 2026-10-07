#include "core/neckwalk.h"

#include "core/files.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <sstream>

const int CLEARED_POINTS = 5; // a round all right: this many notes' worth, times the streak with it
const int FOURTH = 5;         // the next round's key: a fourth up
const double VERDICT_BEFORE_END = 0.5; // beats: the crowd answers this long before the next round
const double MAX_WINDOW_BEATS = 0.45;  // however wide a level's window, it closes before the verdict

const NeckWalkLevel& neckWalkLevel(int index){
    static const NeckWalkLevel LEVELS[NECK_WALK_LEVELS] = {
        // name, key, sequence, rhythm, highest fret, window, points
        { "Easy", "easy", NeckWalkSequence::Straight, NeckWalkRhythm::Even, 12, 0.22f, 100 },
        { "Normal", "normal", NeckWalkSequence::Patterns, NeckWalkRhythm::Mixed, 12, 0.18f, 200 },
        { "Hard", "hard", NeckWalkSequence::Triads, NeckWalkRhythm::Syncopated, 12, 0.15f, 300 },
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

std::vector<NeckStep> neckWalkTriad(int root, const std::vector<int>& tuning, int firstString, int count, int position){
    std::vector<NeckStep> notes;
    for (int string = std::max(0, firstString); string < firstString + count && string < (int)tuning.size(); string++){
        for (int fret = std::max(0, position); fret <= position + 3; fret++){
            const int pitch = tuning[string] + fret, degree = ((pitch - root) % 12 + 12) % 12;
            if (degree != 0 && degree != 4 && degree != 7) continue;
            if (!notes.empty() && pitch <= notes.back().pitch) continue;
            notes.push_back({ pitch, string, fret });
        }
    }
    return notes;
}

// A note's length, in beats, from a few cells (a cell's notes follow each other); a level's rhythm draws its cells
// from its own set, the commoner ones more often
// (in an unnamed namespace: core/rhythm has a RhythmCell of its own, and two types of one name break the program)
namespace {
struct WalkRhythmCell { std::vector<double> lengths; int weight; };
}
static const std::vector<WalkRhythmCell>& rhythmCells(NeckWalkRhythm rhythm){
    static const std::vector<WalkRhythmCell> EVEN = { { { 1.0 }, 1 } };
    static const std::vector<WalkRhythmCell> MIXED = { { { 1.0 }, 4 }, { { 0.5, 0.5 }, 3 }, { { 2.0 }, 1 }, { { 1.5, 0.5 }, 2 } };
    static const std::vector<WalkRhythmCell> SYNCOPATED = { { { 0.5, 0.5 }, 3 }, { { 1.0 }, 1 }, { { 1.5, 0.5 }, 2 }, { { 0.5, 1.0, 0.5 }, 3 } };
    return rhythm == NeckWalkRhythm::Even ? EVEN : rhythm == NeckWalkRhythm::Mixed ? MIXED : SYNCOPATED;
}

// The bars a part takes when its last note is on `last`: a beat after it to spare
static int barsFor(double last){
    return std::max(1, (int)std::ceil((last + 1.0) / NECK_WALK_BAR_BEATS - 1e-9));
}

std::vector<double> neckWalkRhythm(NeckWalkRhythm rhythm, int notes, std::mt19937& random, int& partBars){
    const std::vector<WalkRhythmCell>& cells = rhythmCells(rhythm);
    std::vector<int> weights;
    for (const WalkRhythmCell& cell : cells) weights.push_back(cell.weight);
    std::discrete_distribution<int> pick(weights.begin(), weights.end());
    for (int attempt = 0; attempt < 40; attempt++){
        std::vector<double> lengths;
        while ((int)lengths.size() < notes){
            const WalkRhythmCell& cell = cells[pick(random)];
            lengths.insert(lengths.end(), cell.lengths.begin(), cell.lengths.end());
        }
        // Syncopated: now and then, starting off the beat
        double at = rhythm == NeckWalkRhythm::Syncopated && std::uniform_int_distribution<int>(0, 2)(random) == 0 ? 0.5 : 0.0;
        std::vector<double> onsets;
        for (int i = 0; i < notes; i++){
            onsets.push_back(at);
            at += lengths[i];
        }
        // As few bars as hold it with a beat to spare after the last note; four at the most
        const double last = onsets.empty() ? 0.0 : onsets.back();
        if (last > 4 * NECK_WALK_BAR_BEATS - 1) continue;
        partBars = barsFor(last);
        return onsets;
    }
    std::vector<double> onsets; // too many notes for any of the cells: eighths
    for (int i = 0; i < notes; i++) onsets.push_back(0.5 * i);
    partBars = barsFor(onsets.empty() ? 0.0 : onsets.back());
    return onsets;
}

// Normal's orders over places on strings side by side (`count` of them, 0 the lowest): skipping a string going down
// and back up, from the outside in and back out, or at random (every string once, then any but the one just played)
static std::vector<int> patternOrder(int count, std::mt19937& random){
    std::vector<int> order;
    const int kind = std::uniform_int_distribution<int>(0, 2)(random);
    if (kind == 0 && count >= 4){
        for (int i = count - 1; i >= 2; i--){ order.push_back(i); order.push_back(i - 2); }
        for (int i = 0; i + 2 < count; i++){
            if (order.back() != i) order.push_back(i);
            order.push_back(i + 2);
        }
    } else if (kind <= 1){
        std::vector<int> in;
        for (int low = 0, high = count - 1; low <= high; low++, high--){
            in.push_back(high);
            if (low != high) in.push_back(low);
        }
        order = in;
        for (int i = (int)in.size() - 2; i >= 0; i--) order.push_back(in[i]);
    } else {
        for (int i = 0; i < count; i++) order.push_back(i);
        std::shuffle(order.begin(), order.end(), random);
        const int extra = std::uniform_int_distribution<int>(2, count)(random);
        for (int i = 0; i < extra; i++){
            int string;
            do string = std::uniform_int_distribution<int>(0, count - 1)(random); while (string == order.back());
            order.push_back(string);
        }
    }
    return order;
}

// A sequence of the level's kind in `root`, somewhere on the neck, on `wanted` strings side by side (a triad that
// isn't whole on so few anywhere tried takes one more)
static std::vector<NeckStep> randomSequence(NeckWalkGame& game, int root, int wanted){
    const NeckWalkLevel& level = game.level;
    const int strings = (int)game.tuning.size();
    std::vector<NeckStep> sequence;
    for (int attempt = 0; attempt < 90 && sequence.empty(); attempt++){
        const int count = std::clamp(wanted + attempt / 30, 1, strings);
        const int first = std::uniform_int_distribution<int>(0, strings - count)(game.random);
        const int near = std::uniform_int_distribution<int>(0, std::max(0, level.maxFret - 3))(game.random);
        if (level.sequence == NeckWalkSequence::Straight){
            sequence = neckWalkSteps(root, game.tuning, first, count, near, level.maxFret, 2 * count - 1);
        } else if (level.sequence == NeckWalkSequence::Patterns){
            std::vector<NeckStep> places = neckWalkSteps(root, game.tuning, first, count, near, level.maxFret, count);
            std::reverse(places.begin(), places.end()); // the lowest string first
            if (places.size() < 2) continue;
            for (int index : patternOrder((int)places.size(), game.random)) sequence.push_back(places[index]);
        } else {
            const std::vector<NeckStep> up = neckWalkTriad(root, game.tuning, first, count, near);
            bool degrees[12] = {};
            for (const NeckStep& step : up) degrees[((step.pitch - root) % 12 + 12) % 12] = true;
            if (!degrees[0] || !degrees[4] || !degrees[7]) continue; // the whole triad, its third too
            // Up and back down (or down and back up), or only one way when that's long already
            sequence = up;
            if ((int)up.size() * 2 - 1 <= NECK_WALK_MAX_NOTES) for (int i = (int)up.size() - 2; i >= 0; i--) sequence.push_back(up[i]);
            if (std::uniform_int_distribution<int>(0, 1)(game.random) == 1) std::reverse(sequence.begin(), sequence.end());
        }
        if ((int)sequence.size() > NECK_WALK_MAX_NOTES) sequence.resize(NECK_WALK_MAX_NOTES);
    }
    return sequence;
}

// Round `index` (from 0), from `firstBar` at `startTime`: its key (two rounds a key, then a fourth up), on as many
// strings and at the tempo the player is at now
static NeckWalkRound makeRound(NeckWalkGame& game, int index, int firstBar, double startTime){
    NeckWalkRound round;
    round.root = (game.firstRoot + FOURTH * (index / NECK_WALK_ROUNDS_A_KEY)) % 12;
    round.firstBar = firstBar;
    round.tempo = game.tempo;
    round.beatSeconds = 60.0 / game.tempo;
    round.startTime = startTime;
    round.walk = randomSequence(game, round.root, game.strings);
    std::vector<int> on;
    for (const NeckStep& step : round.walk) if (std::find(on.begin(), on.end(), step.string) == on.end()) on.push_back(step.string);
    round.strings = (int)on.size();
    round.onsets = neckWalkRhythm(game.level.rhythm, (int)round.walk.size(), game.random, round.partBars);
    return round;
}

// Where a round ends: the computer's part and the player's, at its tempo
static double roundEnd(const NeckWalkRound& round){
    return round.startTime + 2.0 * round.partBars * NECK_WALK_BAR_BEATS * round.beatSeconds;
}

// The next round: the one prepared a round ahead, and the one after it prepared
static void nextRound(NeckWalkGame& game, int round){
    game.round = round;
    game.now = game.next;
    game.judged = false;
    game.notes.assign(game.now.walk.size(), WalkNote::Due);
    game.heardWrong.assign(game.now.walk.size(), false);
    game.mostStrings = std::max(game.mostStrings, game.now.strings);
    game.fastest = std::max(game.fastest, game.now.tempo);
    game.next = makeRound(game, round + 1, game.now.firstBar + 2 * game.now.partBars, roundEnd(game.now));
}

void startNeckWalk(NeckWalkGame& game, const NeckWalkSetup& setup, const std::vector<int>& tuning, unsigned seed,
                   double startTime){
    game = NeckWalkGame{};
    game.setup = setup;
    game.levelIndex = game.setup.level = std::clamp(setup.level, 0, NECK_WALK_LEVELS - 1);
    game.level = neckWalkLevel(game.levelIndex);
    game.tuning = tuning;
    game.random.seed(seed);
    game.startTime = startTime;
    game.setup.bpm = std::clamp(setup.bpm, NECK_WALK_MIN_BPM, NECK_WALK_MAX_BPM);
    game.tempo = (float)game.setup.bpm;
    game.lives = NECK_WALK_LIVES;
    if (tuning.empty()){
        game.over = true;
        return;
    }
    game.setup.strings = game.strings = std::clamp(setup.strings, std::min(NECK_WALK_MIN_STRINGS, (int)tuning.size()), (int)tuning.size());
    game.firstRoot = std::uniform_int_distribution<int>(0, 11)(game.random);
    game.next = makeRound(game, 0, NECK_WALK_INTRO_BARS, startTime + NECK_WALK_INTRO_BARS * NECK_WALK_BAR_BEATS * 60.0 / game.tempo);
    nextRound(game, 0);
}

// A bar is the round's it's in, at that round's tempo (the bar before the first round, at the first round's)
static const NeckWalkRound& roundOfBar(const NeckWalkGame& game, int bar){
    return bar >= game.next.firstBar ? game.next : game.now;
}

double neckWalkBarTime(const NeckWalkGame& game, int bar){
    const NeckWalkRound& round = roundOfBar(game, bar);
    return round.startTime + (double)(bar - round.firstBar) * NECK_WALK_BAR_BEATS * round.beatSeconds;
}

int neckWalkBarAt(const NeckWalkGame& game, double time){
    const NeckWalkRound& round = time >= game.next.startTime ? game.next : game.now;
    return round.firstBar + (int)std::floor((time - round.startTime) / (NECK_WALK_BAR_BEATS * round.beatSeconds));
}

double neckWalkRoundStart(const NeckWalkGame&, const NeckWalkRound& round){
    return round.startTime;
}

double neckWalkShowTime(const NeckWalkGame&, const NeckWalkRound& round, int note){
    return round.startTime + round.onsets[note] * round.beatSeconds;
}

double neckWalkNoteTime(const NeckWalkGame& game, int note){
    return neckWalkShowTime(game, game.now, note) + game.now.partBars * NECK_WALK_BAR_BEATS * game.now.beatSeconds;
}

double neckWalkVerdictTime(const NeckWalkGame& game){
    return roundEnd(game.now) - VERDICT_BEFORE_END * game.now.beatSeconds;
}

static double window(const NeckWalkGame& game){
    return std::min((double)game.level.windowSeconds, MAX_WINDOW_BEATS * game.now.beatSeconds);
}

NeckWalkEvents neckWalkPlayed(NeckWalkGame& game, int pitch, double time){
    NeckWalkEvents events;
    if (game.over || game.judged || pitch < 0) return events;
    int nearest = -1;
    for (int i = 0; i < (int)game.notes.size(); i++){
        if (game.notes[i] != WalkNote::Due || std::abs(time - neckWalkNoteTime(game, i)) > window(game)) continue;
        if (nearest < 0 || std::abs(time - neckWalkNoteTime(game, i)) < std::abs(time - neckWalkNoteTime(game, nearest))) nearest = i;
    }
    if (nearest < 0) return events;
    if (pitch % 12 != game.now.walk[nearest].pitch % 12){
        game.heardWrong[nearest] = true;
        return events;
    }
    game.notes[nearest] = WalkNote::Right;
    game.score += (long long)game.level.points * (game.streak + 1);
    game.rightByNote[game.now.root]++;
    events.right = nearest;
    return events;
}

NeckWalkVerdict neckWalkVerdictOf(const NeckWalkGame& game){
    const long right = std::count(game.notes.begin(), game.notes.end(), WalkNote::Right);
    if (!game.notes.empty() && right == (long)game.notes.size()) return NeckWalkVerdict::Cheer;
    return right * 2 >= (long)game.notes.size() && right > 0 ? NeckWalkVerdict::Claps : NeckWalkVerdict::Aww;
}

NeckWalkEvents neckWalkUpdate(NeckWalkGame& game, double time){
    NeckWalkEvents events;
    if (game.over) return events;
    for (int i = 0; i < (int)game.notes.size(); i++){
        if (game.notes[i] != WalkNote::Due || time <= neckWalkNoteTime(game, i) + window(game)) continue;
        game.notes[i] = game.heardWrong[i] ? WalkNote::Wrong : WalkNote::Missed;
        game.wrongByNote[game.now.root]++;
        (game.heardWrong[i] ? events.wrong : events.missed) = i;
    }
    if (!game.judged && time >= neckWalkVerdictTime(game)){
        game.judged = true;
        events.verdict = true;
        events.how = neckWalkVerdictOf(game);
        if (events.how == NeckWalkVerdict::Cheer){
            game.streak++;
            game.bestStreak = std::max(game.bestStreak, game.streak);
            game.cleared++;
            game.score += (long long)CLEARED_POINTS * game.level.points * game.streak;
            // Enough of them on this many strings: one more, from the round after next (the next is made already);
            // and, counted apart, enough at this tempo: faster
            if (game.setup.stringsEvery > 0 && game.now.strings >= game.strings && game.strings < (int)game.tuning.size()
                && ++game.clearsOnStrings >= game.setup.stringsEvery){
                game.strings++;
                game.clearsOnStrings = 0;
                events.moreStrings = true;
            }
            if (game.setup.tempoEvery > 0 && game.now.tempo >= game.tempo && game.tempo < NECK_WALK_MAX_BPM
                && ++game.clearsOnTempo >= game.setup.tempoEvery){
                game.tempo = std::min((float)NECK_WALK_MAX_BPM, game.tempo + NECK_WALK_TEMPO_STEP);
                game.clearsOnTempo = 0;
                events.faster = true;
            }
        } else if (events.how == NeckWalkVerdict::Claps){
            game.streak = 0; // not a fail: no life lost, but the streak is of walks all right
        } else {
            game.streak = 0;
            game.lives--;
            if (game.lives <= 0){
                game.over = true;
                events.over = true;
                return events;
            }
        }
    }
    if (game.judged && time >= neckWalkRoundStart(game, game.next)){
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
            ss >> record.bpm >> record.strings >> record.fastest;
            stats.games.push_back(record);
        } else if (kind == "note"){
            int note, right, wrong;
            if (ss >> note >> right >> wrong && note >= 0 && note < 12){
                stats.right[note] = right;
                stats.wrong[note] = wrong;
            }
        } else if (kind == "choice"){
            std::string level;
            NeckWalkSetup choice;
            if (ss >> level >> choice.bpm && neckWalkLevelIndex(level) >= 0){
                choice.level = neckWalkLevelIndex(level);
                ss >> choice.strings >> choice.stringsEvery >> choice.tempoEvery; // older files stop sooner: the defaults
                stats.choice = choice;
                stats.chosen = true;
            }
        }
    }
    return stats;
}

bool saveNeckWalkStats(const std::string& path, const NeckWalkStats& stats, std::string& error){
    std::ostringstream out;
    out << "# hardthz neck walk: game <date> <score> <rounds cleared> <best streak> <level> <bpm> <most strings> <fastest bpm>; "
           "note <pitch class> <right> <wrong>; choice <level> <bpm> <strings> <rounds a string> <rounds a tempo step>\n";
    for (const NeckWalkRecord& game : stats.games)
        out << "game " << game.date << " " << game.score << " " << game.cleared << " " << game.bestStreak << " " << game.level << " "
            << game.bpm << " " << game.strings << " " << game.fastest << "\n";
    for (int note = 0; note < 12; note++)
        if (stats.right[note] || stats.wrong[note]) out << "note " << note << " " << stats.right[note] << " " << stats.wrong[note] << "\n";
    if (stats.chosen){
        const NeckWalkSetup& choice = stats.choice;
        out << "choice " << neckWalkLevel(choice.level).key << " " << choice.bpm << " " << choice.strings << " " << choice.stringsEvery << " "
            << choice.tempoEvery << "\n";
    }
    return writeFileAtomically(path, out.str(), error);
}

void addNeckWalkGame(NeckWalkStats& stats, const NeckWalkGame& game, const std::string& date){
    stats.games.push_back({ date, game.score, game.cleared, game.bestStreak, game.level.key, game.setup.bpm, game.mostStrings,
                            (int)std::lround(game.fastest) });
    for (int note = 0; note < 12; note++){
        stats.right[note] += game.rightByNote[note];
        stats.wrong[note] += game.wrongByNote[note];
    }
    stats.choice = game.setup;
    stats.chosen = true;
}

long long neckWalkBest(const NeckWalkStats& stats, int levelIndex){
    long long best = 0;
    for (const NeckWalkRecord& game : stats.games)
        if (game.level == neckWalkLevel(levelIndex).key) best = std::max(best, game.score);
    return best;
}
