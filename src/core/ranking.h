#pragma once

#include "core/chart.h"

#include <string>
#include <vector>

// Competing: how good a run was, in the numbers rhythm game players compare, and the best runs kept for every part
// of every song. Pure logic; the play screen feeds it, the results and song select screens show it.

// Accuracy the way osu! counts it: a perfect is worth all of a note, a good a third, a miss nothing. 0 to 100.
float runAccuracy(int perfect, int good, int miss);

enum class Grade { SS, S, A, B, C, D };
// SS: every note perfect. S: 95% or more with nothing missed. Then A from 90%, B from 80%, C from 70%, D below.
Grade gradeFor(float accuracy, int misses);
const char* gradeName(Grade grade);

// How steady the timing was: the average error (+ early, - late, in ms) and the unstable rate, osu!'s number for
// the spread: the standard deviation of the errors, times ten. Lower is steadier; 0 for fewer than two hits.
struct TimingStats {
    float meanMs = 0.0f;
    float unstableRate = 0.0f;
};
TimingStats timingStats(const std::vector<float>& errorsMs);

// One run, as it's kept
struct RunRecord {
    long long score = 0;
    float accuracy = 0.0f;
    int maxCombo = 0;
    int perfect = 0, good = 0, miss = 0;
    float unstableRate = 0.0f;
    bool withInstrument = false; // played on the instrument, not the keyboard
    std::string date;            // YYYY-MM-DD
    Grade grade() const { return gradeFor(accuracy, miss); }
    bool fullCombo() const { return miss == 0; }
};

// A fingerprint of a part as it's played: its tuning, notes and timing. Edit the chart and the part has another
// fingerprint, so its records start over instead of mixing runs of two different charts.
std::string partFingerprint(const Chart& chart, int part);

const int KEPT_RUNS = 20;
// Adds a run to a part's records (the best first, by score) and keeps the best KEPT_RUNS. Returns where it placed,
// 0 for a new best, -1 if it didn't make the list.
int addRun(std::vector<RunRecord>& records, const RunRecord& run);

// Where a part's records are kept: <recordsDir>/<song id>-part<n>-<fingerprint>.txt
std::string recordsPath(const std::string& recordsDir, const std::string& songId, int part, const std::string& fingerprint);

// Records files load leniently, like the progress files: they're the player's own
std::vector<RunRecord> loadRuns(const std::string& path);
bool saveRuns(const std::string& path, const std::vector<RunRecord>& records, std::string& error);

// Every run of a part, in the order they were played, for showing progress: beside its records file, with
// "-history" in its name. The oldest go once there are more than KEPT_HISTORY.
const int KEPT_HISTORY = 200;
std::string historyPath(const std::string& recordsPath);
// Records kept before there was a history seed it: their runs, oldest date first
std::vector<RunRecord> loadHistory(const std::string& historyPath, const std::vector<RunRecord>& records);
// Adds a run at the end and saves; `history` is as loadHistory gave it
bool addToHistory(const std::string& historyPath, std::vector<RunRecord>& history, const RunRecord& run, std::string& error);
