#include "core/necktrainer.h"

#include "core/music.h"

#include <algorithm>

const int POSITION_SPAN = 3; // a position reaches from the index finger's fret to the pinky's, 3 frets up

// The scale's notes in a position, ascending: string by string, low to high, each string's notes within the hand's
// reach, a note already taken on a lower string not taken again (where two strings share it, the lower one plays it)
static std::vector<NeckStep> positionNotes(int root, const ScaleInfo& scale, const std::vector<int>& tuning, int position, int frets){
    std::vector<NeckStep> notes;
    const int first = std::max(0, position), last = std::min(frets, position + POSITION_SPAN);
    for (int string = 0; string < (int)tuning.size(); string++){
        for (int fret = position == 0 ? 0 : first; fret <= last; fret++){
            const int pitch = tuning[string] + fret;
            const int degree = ((pitch - root) % 12 + 12) % 12;
            if (std::find(scale.steps.begin(), scale.steps.end(), degree) == scale.steps.end()) continue;
            if (!notes.empty() && pitch <= notes.back().pitch) continue;
            notes.push_back({ pitch, string, fret });
        }
    }
    return notes;
}

// The scale three notes on each string, from the first of its notes at `position` or above on the lowest string
static bool threePerString(int root, const ScaleInfo& scale, const std::vector<int>& tuning, int position, int frets,
                           std::vector<NeckStep>& out, std::string& error){
    std::vector<int> pitches = scalePitches(root, scale, tuning.front() + std::max(0, position), tuning.back() + frets);
    if (pitches.size() > 3 * tuning.size()) pitches.resize(3 * tuning.size());
    std::vector<FretPosition> places;
    if (!fingerPitches(pitches, tuning, Fingering::ThreeNotesPerString, position, places, error)) return false;
    out.clear();
    for (size_t i = 0; i < pitches.size(); i++){
        if (places[i].fret > frets){
            error = "it goes past the last fret";
            return false;
        }
        out.push_back({ pitches[i], places[i].stringIndex, places[i].fret });
    }
    return true;
}

bool neckSteps(const NeckRoutine& routine, const std::vector<int>& tuning, int frets, std::vector<NeckStep>& out, std::string& error){
    out.clear();
    if (tuning.empty()){
        error = "no strings";
        return false;
    }
    // One note on every string: its first place on each, from the position up, low string to high and back
    if (routine.pattern == NeckPattern::EveryString){
        std::vector<NeckStep> up;
        for (int string = 0; string < (int)tuning.size(); string++){
            for (int fret = std::max(0, routine.position); fret <= frets; fret++){
                if ((tuning[string] + fret) % 12 != routine.notePitchClass) continue;
                up.push_back({ tuning[string] + fret, string, fret });
                break;
            }
        }
        out = up;
        for (int i = (int)up.size() - 2; i >= 0; i--) out.push_back(up[i]);
        return !out.empty() || (error = "the note isn't on this neck", false);
    }
    const ScaleInfo* scale = findScale(routine.scale);
    if (!scale){
        error = "unknown scale '" + routine.scale + "'";
        return false;
    }
    std::vector<NeckStep> notes;
    if (routine.fingering == Fingering::ThreeNotesPerString){
        if (!threePerString(routine.rootPitchClass, *scale, tuning, routine.position, frets, notes, error)) return false;
    } else {
        notes = positionNotes(routine.rootPitchClass, *scale, tuning, routine.position, frets);
    }
    const int n = (int)notes.size();
    const int reach = routine.pattern == NeckPattern::Thirds ? 2 : routine.pattern == NeckPattern::Triads ? 4 : 0;
    if (n <= reach){
        error = "too few of the scale's notes there";
        return false;
    }
    switch (routine.pattern){
        case NeckPattern::Straight:
            out = notes;
            for (int i = n - 2; i >= 0; i--) out.push_back(notes[i]);
            break;
        case NeckPattern::Thirds:
            for (int i = 0; i + 2 < n; i++){ out.push_back(notes[i]); out.push_back(notes[i + 2]); }
            for (int i = n - 1; i - 2 >= 0; i--){ out.push_back(notes[i]); out.push_back(notes[i - 2]); }
            break;
        case NeckPattern::Triads:
            for (int i = 0; i + 4 < n; i++){ out.push_back(notes[i]); out.push_back(notes[i + 2]); out.push_back(notes[i + 4]); }
            for (int i = n - 1; i - 4 >= 0; i--){ out.push_back(notes[i]); out.push_back(notes[i - 2]); out.push_back(notes[i - 4]); }
            break;
        case NeckPattern::EveryString:
            break;
    }
    return true;
}

const char* neckPatternName(NeckPattern pattern){
    switch (pattern){
        case NeckPattern::EveryString: return "on every string";
        case NeckPattern::Straight: return "up and down";
        case NeckPattern::Thirds: return "in thirds";
        case NeckPattern::Triads: return "in triads";
    }
    return "";
}

std::string neckRoutineName(const NeckRoutine& routine){
    if (routine.pattern == NeckPattern::EveryString)
        return std::string(pitchClassName(routine.notePitchClass)) + " on every string, from fret " + std::to_string(routine.position);
    const ScaleInfo* scale = findScale(routine.scale);
    std::string name = std::string(pitchClassName(routine.rootPitchClass)) + " " + (scale ? scale->displayName : routine.scale);
    name += routine.fingering == Fingering::ThreeNotesPerString ? ", three notes per string" : ", position " + std::to_string(routine.position);
    return name + ", " + neckPatternName(routine.pattern);
}

void startNeckRun(NeckRun& run, const std::vector<NeckStep>& steps){
    run = NeckRun{};
    run.steps = steps;
}

bool playNeckNote(NeckRun& run, int pitch, double time){
    if (neckRunDone(run)) return false;
    if (pitch != run.steps[run.next].pitch){
        run.mistakes++;
        run.lastWrongPitch = pitch;
        return false;
    }
    if (run.startedAt < 0.0) run.startedAt = time;
    run.stepSeconds.push_back(run.lastAt < 0.0 ? 0.0f : (float)(time - run.lastAt));
    run.lastAt = time;
    run.next++;
    return true;
}

bool neckRunDone(const NeckRun& run){
    return run.next >= run.steps.size();
}

double neckRunSeconds(const NeckRun& run){
    return run.startedAt < 0.0 ? 0.0 : run.lastAt - run.startedAt;
}
