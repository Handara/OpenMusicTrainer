#pragma once

#include <map>
#include <string>

// How many times each exercise or lesson was taken up, and when last: one file for them all (the progress folder's
// plays.txt), one line each, "<id> <times> <YYYY-MM-DD>". For the Learn menu's "done 12 times, last on ...".
struct PlayCount {
    int times = 0;
    std::string last; // YYYY-MM-DD, "" for never
};
std::map<std::string, PlayCount> loadPlays(const std::string& path); // empty if there's none yet
// One more time for `id`, on `date`: the file read, counted, written back
bool recordPlay(const std::string& path, const std::string& id, const std::string& date, std::string& error);
