#include "core/scales.h"

#include "core/chart.h"
#include "core/music.h"

#include <algorithm>
#include <cctype>

const std::vector<ScaleInfo>& allScales(){
    // A function-local static: built the first time it's asked for, then kept for the rest of the program
    static const std::vector<ScaleInfo> scales = {
        { "major", "Major", {0, 2, 4, 5, 7, 9, 11} },
        { "minor", "Natural minor", {0, 2, 3, 5, 7, 8, 10} },
        { "harmonic_minor", "Harmonic minor", {0, 2, 3, 5, 7, 8, 11} },
        { "melodic_minor", "Melodic minor", {0, 2, 3, 5, 7, 9, 11} },
        { "dorian", "Dorian", {0, 2, 3, 5, 7, 9, 10} },
        { "phrygian", "Phrygian", {0, 1, 3, 5, 7, 8, 10} },
        { "lydian", "Lydian", {0, 2, 4, 6, 7, 9, 11} },
        { "mixolydian", "Mixolydian", {0, 2, 4, 5, 7, 9, 10} },
        { "locrian", "Locrian", {0, 1, 3, 5, 6, 8, 10} },
        { "major_pentatonic", "Major pentatonic", {0, 2, 4, 7, 9} },
        { "minor_pentatonic", "Minor pentatonic", {0, 3, 5, 7, 10} },
        { "blues", "Blues", {0, 3, 5, 6, 7, 10} },
        { "chromatic", "Chromatic", {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11} },
    };
    return scales;
}

const ScaleInfo* findScale(const std::string& name){
    for (const ScaleInfo& scale : allScales()) if (name == scale.name) return &scale;
    return nullptr;
}

bool parsePitchClass(const std::string& name, int& pitchClass){
    const int LETTER_PITCHES[7] = { 9, 11, 0, 2, 4, 5, 7 }; // A B C D E F G
    if (name.empty() || name.size() > 2) return false;
    char letter = (char)std::toupper((unsigned char)name[0]);
    if (letter < 'A' || letter > 'G') return false;
    pitchClass = LETTER_PITCHES[letter - 'A'];
    if (name.size() == 2){
        if (name[1] == '#') pitchClass += 1;
        else if (name[1] == 'b') pitchClass += 11; // a flat: one down, kept in 0..11
        else return false;
    }
    pitchClass %= 12;
    return true;
}

std::vector<int> scalePitches(int rootPitchClass, const ScaleInfo& scale, int lowest, int highest){
    std::vector<int> pitches;
    for (int pitch = lowest; pitch <= highest; pitch++){
        int step = ((pitch - rootPitchClass) % 12 + 12) % 12;
        if (std::find(scale.steps.begin(), scale.steps.end(), step) != scale.steps.end()) pitches.push_back(pitch);
    }
    return pitches;
}

static std::string noteName(int pitch){
    return std::string(pitchClassName(pitch)) + std::to_string(pitchOctave(pitch));
}

bool fingerPitches(const std::vector<int>& pitches, const std::vector<int>& tuning, Fingering fingering, int position,
                   std::vector<FretPosition>& out, std::string& error){
    out.clear();
    const int strings = (int)tuning.size();

    if (fingering == Fingering::ThreeNotesPerString){
        for (int i = 0; i < (int)pitches.size(); i++){
            int string = i / 3;
            int fret = string < strings ? pitches[i] - tuning[string] : -1;
            if (string >= strings || fret < 0 || fret > MAX_FRET){
                error = "three notes per string: no room for " + noteName(pitches[i]);
                return false;
            }
            out.push_back({string, fret});
        }
        return true;
    }

    // The frets the hand covers: index finger on `position`, one fret below for a stretch, pinky 3 frets up
    const int lowFret = std::max(0, position - 1);
    const int highFret = std::max(position, 1) + 3;
    int string = 0;
    for (int pitch : pitches){
        while (string < strings && pitch - tuning[string] > highFret) string++; // too high here: next string
        if (string == strings || pitch - tuning[string] < lowFret){
            error = noteName(pitch) + " doesn't fit around fret " + std::to_string(position);
            return false;
        }
        out.push_back({string, pitch - tuning[string]});
    }
    return true;
}
