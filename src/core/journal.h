#pragma once

#include <string>
#include <vector>

// The player's journal: one line for every run they play (a drill's pass, a run of notes, a song, a game) and every
// chapter they pass, in the order played. Everything about their progress is worked out from it (core/profile): their
// XP and level, their streak, the minutes they practiced each day, how well they know each note, their achievements.
// So none of those can disagree with another, and a new achievement counts what was played before it existed. A text
// file (the progress folder's journal.txt), only ever added to; read leniently, like every progress file.

enum class ActivityKind { Drill, Notes, Song, Game, Chapter, Count };

// How a note went in a run: played right (first time, for untimed runs), of how many times it was asked
struct NoteTally {
    int pitch = 0; // MIDI
    int right = 0;
    int asked = 0;
};

struct Activity {
    ActivityKind kind = ActivityKind::Drill;
    std::string date;           // YYYY-MM-DD, the player's own day
    int minute = 0;             // of that day, from midnight
    std::string id;             // the drill's, the song's (with its part), the course's chapter
    std::string title;          // as shown: "E and F, shown where"
    std::string instrument;     // guitar, bass, piano, keys (a song's part played on the keyboard)
    float seconds = 0.0f;       // how long it took to play
    int right = 0, total = 0;   // notes (a drill's, a song's) or prompts (a run of notes) right, of how many; a chapter's stars
    int tempo = 0;              // bpm (drills, games)
    bool clean = false;         // a drill's pass at its pass mark; a run of notes passed; a song with nothing missed
    bool challenge = false;     // a drill's clean pass at its challenge tempo: it passes the drill
    bool band = false;          // played with the backing band
    std::string grade;          // a song's ("S", "A"...)
    int rounds = 0;             // a game's
    int combo = 0;              // the most notes in a row played right (a drill's pass, a song)
    bool unitDone = false;      // a chapter: its level complete with it
    bool courseDone = false;    //   the whole course
    std::vector<NoteTally> notes;

    bool perfect() const { return total > 0 && right == total; }
};

std::string writeActivity(const Activity& activity); // one line, no newline
bool readActivity(const std::string& line, Activity& activity); // false for a line that isn't one (skipped)

std::vector<Activity> loadJournal(const std::string& path); // oldest first; empty if there's none yet
bool appendToJournal(const std::string& path, const Activity& activity, std::string& error);
