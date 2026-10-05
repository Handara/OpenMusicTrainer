#include "core/groove.h"

#include "core/scales.h"

#include <cctype>
#include <fstream>
#include <sstream>

// "F#3", "Bb1", "C4" -> MIDI (C4 = 60); false if it isn't one
static bool parseNote(const std::string& text, int& pitch){
    const int LETTER_PITCHES[7] = { 9, 11, 0, 2, 4, 5, 7 }; // A B C D E F G
    if (text.size() < 2) return false;
    const char letter = (char)std::toupper((unsigned char)text[0]);
    if (letter < 'A' || letter > 'G') return false;
    int semitone = LETTER_PITCHES[letter - 'A'];
    size_t at = 1;
    if (text[at] == '#'){ semitone++; at++; }
    else if (text[at] == 'b'){ semitone--; at++; }
    if (at >= text.size()) return false;
    try {
        size_t used = 0;
        const int octave = std::stoi(text.substr(at), &used);
        if (at + used != text.size()) return false;
        pitch = (octave + 1) * 12 + semitone;
    } catch (...){
        return false;
    }
    return pitch >= 0 && pitch <= 127;
}

static bool parseDrum(const std::string& name, KitDrum& drum){
    if (name == "kick") drum = KitDrum::Kick;
    else if (name == "snare") drum = KitDrum::Snare;
    else if (name == "hat") drum = KitDrum::Hat;
    else if (name == "open") drum = KitDrum::OpenHat;
    else if (name == "crash") drum = KitDrum::Crash;
    else return false;
    return true;
}

bool parseGroove(const std::string& text, Groove& groove, std::string& error){
    groove = Groove{};
    std::istringstream lines(text);
    std::string line;
    int number = 0;
    while (std::getline(lines, line)){
        number++;
        const size_t comment = line.find('#');
        // '#' starts a comment, except in a note's sharp (F#3) or the key (key F#): only after a space or at the start
        if (comment != std::string::npos && (comment == 0 || std::isspace((unsigned char)line[comment - 1]))) line.resize(comment);
        std::istringstream words(line);
        std::string first;
        if (!(words >> first)) continue;
        auto fail = [&](const std::string& why){
            error = "line " + std::to_string(number) + ": " + why;
            return false;
        };
        if (first == "tempo"){
            if (!(words >> groove.tempo) || groove.tempo < 20.0f || groove.tempo > 400.0f) return fail("tempo: a number of beats a minute");
        } else if (first == "key"){
            std::string key;
            if (!(words >> key) || !parsePitchClass(key, groove.key)) return fail("key: a note's name, like F#");
        } else if (first == "beats"){
            if (!(words >> groove.barBeats) || groove.barBeats < 1 || groove.barBeats > 16) return fail("beats: how many in a bar");
        } else if (first == "phrase"){
            groove.phrase.clear();
            std::string bar;
            while (words >> bar) groove.phrase.push_back(bar);
        } else {
            // A hit, in one bar or several: <bars> <part> <beat> ...
            std::string partName;
            GrooveHit hit;
            if (!(words >> partName >> hit.beat)) return fail("expected <bars> <part> <beat> ...");
            if (partName == "drums"){
                hit.part = GroovePart::Drums;
                std::string drum;
                if (!(words >> drum) || !parseDrum(drum, hit.drum)) return fail("a drum: kick, snare, hat, open or crash");
            } else if (partName == "guitar" || partName == "bass"){
                hit.part = partName == "bass" ? GroovePart::Bass : GroovePart::Guitar;
                std::string note;
                if (!(words >> hit.length >> note) || !parseNote(note, hit.pitch)) return fail("expected <length in beats> <note, like F#3>");
            } else {
                return fail("unknown part '" + partName + "' (guitar, bass or drums)");
            }
            std::istringstream names(first);
            std::string bar;
            while (std::getline(names, bar, ',')) if (!bar.empty()) groove.bars[bar].push_back(hit);
        }
    }
    if (groove.phrase.empty()){
        error = "no phrase: which bars, in what order";
        return false;
    }
    for (const std::string& bar : groove.phrase){
        if (!groove.bars.count(bar)){
            error = "the phrase's bar '" + bar + "' has nothing in it";
            return false;
        }
    }
    return true;
}

bool loadGroove(const std::string& path, Groove& groove, std::string& error){
    std::ifstream in(path);
    if (!in){
        error = "can't open " + path;
        return false;
    }
    std::stringstream text;
    text << in.rdbuf();
    if (!parseGroove(text.str(), groove, error)){
        error = path + ": " + error;
        return false;
    }
    return true;
}

int grooveShift(const Groove& groove, int root){
    int shift = ((root - groove.key) % 12 + 12) % 12; // 0..11 up
    if (shift > 9) shift -= 12;                       // 10, 11 up: 2, 1 down
    return shift;
}

std::vector<GrooveHit> grooveBar(const Groove& groove, int barInPhrase, int root){
    if (groove.phrase.empty()) return {};
    const int size = (int)groove.phrase.size();
    const auto found = groove.bars.find(groove.phrase[((barInPhrase % size) + size) % size]);
    if (found == groove.bars.end()) return {};
    std::vector<GrooveHit> hits = found->second;
    const int shift = grooveShift(groove, root);
    for (GrooveHit& hit : hits) if (hit.part != GroovePart::Drums) hit.pitch += shift;
    return hits;
}
