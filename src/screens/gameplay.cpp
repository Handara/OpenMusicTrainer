#include "screens/gameplay.h"

#include "app/screenrecorder.h"
#include "app/videoconvert.h"
#include "audio/audio.h"
#include "core/chart.h"
#include "core/chords.h"
#include "core/judge.h"
#include "core/music.h"
#include "core/paths.h"
#include "core/pianokeys.h"
#include "core/rhythmmode.h"
#include "core/score.h"
#include "core/tuningcheck.h"
#include "imgui.h"
#include "input/midi.h"
#include "input/noteinput.h"
#include "input/pianokeys.h"
#include "input/synthmonitor.h"
#include "raylib.h"
#include "ui/hitfeedback.h"
#include "ui/menulist.h"
#include "ui/theme.h"
#include "video/video.h"
#include "views/neckview.h"
#include "views/noteviews.h"
#include "views/staff.h"
#include "views/rhythmlane.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <thread>
#include <vector>

struct GameState {
    int score;
    int combo;
    int maxCombo;
    int multiplier; // increments on a perfect hit, unchanged on near, resets on miss
    float rhythm; // 0..1, the "rhythm" meter
    int perfectCount;
    int nearCount;
    int missCount;
    std::vector<float> errorsMs; // every hit's timing (+ early, - late): for the run's stats
};

const int MAX_LANES = 8; // an 8-string guitar; on the keyboard, the number keys 1 to 8
const int HIT_LINE_X = 180;
const float RHYTHM_FILL_PER_PERFECT = 0.12f;

const int laneKeys[MAX_LANES] = { KEY_ONE, KEY_TWO, KEY_THREE, KEY_FOUR, KEY_FIVE, KEY_SIX, KEY_SEVEN, KEY_EIGHT };

const int MAX_MULTIPLIER = 4;
const double LEAD_IN_S = 2.0; // starting part-way into a song, it plays this long before the first note
const double MIN_COUNT_IN_S = 1.5; // from the top, a bar is counted in; two when one is shorter than this (fast songs)
const double TRIM_FADE_S = 0.4;    // a trimmed song's end fades out over this long rather than being cut
const double RESUME_RUNUP_S = 1.5; // resuming, the song picks up this long before where it was paused

const float PASS_TAIL_S = 0.35f;   // practising, a pass ends this long after its section does: its last note's judgement
const float TEMPO_BANNER_S = 2.0f; // how long the new tempo shows after a pass raises it
// Note by note, the song slows as a note still to play comes near, down to a crawl, its sound going on all the time;
// once the note's played, it eases back up
const float SLOW_FROM_S = (float)PERFECT_WINDOW_S; // slowing begins where a perfect hit would: played well, nothing slows
const float CRAWL = 0.03f;        // the slowest, against the practice's tempo
const float EASE_UP_S = 0.08f;    // back up to speed over about this long

static HitFeedback feedback; // the judgements, timing bar and combo shown over the play screen
// Played slower (practising), the song's seconds go by slower than the clock's: a note heard 0.1 s ago (the clock's)
// is 0.07 s back in a song at 70%. And waiting on a note (note by note), the song stands still: whatever's played is
// played now, at the note.
static float inputScale = 1.0f;
static bool frozenTime = false;
static float frozenAt = 0.0f; // waiting on a note: whatever's played counts as played on it
// Every note heard from the instrument in this run, for the comparison at its end
static std::vector<HeardPitch> heardLog;

// A check being recorded (F9: this song and every one after, until pressed again): the instrument as it came in
// (input/noteinput: <base>.wav, and what the detectors found in it, in seconds into the recording) and, here, what
// was heard and judged on the song's clock, with the two clocks side by side now and then. Saved when the song stops.
static bool recordChecks = false;
static std::vector<std::string> checkLog;
static float checkSongTime = 0.0f;  // the song's time this frame, for the lines written where it isn't known
static double lastSync = -1.0;
static std::string checkSaved;      // where the last one went, shown a moment
static double checkSavedAt = -100.0;
// ...and the screen with it, when FFmpeg is there (the video add-on): its pictures as the song goes, then the sound
// put under them (the instrument as recorded, and the song's audio) on a worker thread, as <base>.mp4
static std::string checkBase;       // the files' path, without their endings
static std::string checkSongAudio;  // the song's audio file
static double firstSyncRecording = -1.0, firstSyncSong = 0.0;
static std::thread checkVideo;
static std::atomic<bool> checkVideoBusy{false};
static void logCheck(const std::string& line){
    if (inputRecording()) checkLog.push_back(line);
}
static void beginCheck(); // further down: they need the song
static void saveCheck();
static std::string pitchText(int pitch){
    return pitch < 0 ? std::string("?") : std::string(TextFormat("%s%d", pitchClassName(pitch), pitchOctave(pitch)));
}

static void logHeard(double time, int pitch){
    logCheck(TextFormat("%9.3f  heard   %s", time, pitchText(pitch).c_str()));
    // A pluck of several notes is heard twice: one note at once, all of them a moment later
    for (auto it = heardLog.rbegin(); it != heardLog.rend() && time - it->time < 0.08; ++it) if (it->pitch == pitch && std::fabs(it->time - time) < 0.08) return;
    heardLog.push_back({ (float)time, pitch });
}

// When in the song something played `age` seconds ago (the clock's) was played, the input's own delay counted
static double heardAt(float songTime, double age, float inputOffset){
    return frozenTime ? (double)frozenAt : songTime - (age + inputOffset) * inputScale;
}
static bool instrumentHitSounds = false; // a drop on each hit: playing an instrument, which gives no sound of its own to the game

// Where a judgement is shown: over its note on the neck, when the neck is drawn, else over it in the sheet music;
// else at the hit line (x < 0)
static ImVec2 judgementAnchor(const std::vector<PlayNote>& notes, int index){
    float x, y, radius;
    if (index < 0 || index >= (int)notes.size()) return ImVec2(-1.0f, -1.0f);
    if (neckNoteAt(notes[index], x, y, radius)) return ImVec2(x, y - radius - 24.0f * GetScreenHeight() / 720.0f);
    if (staffNoteAt(index, x, y)) return ImVec2(x, y - 12.0f * GetScreenHeight() / 720.0f);
    return ImVec2(-1.0f, -1.0f);
}

static void scoreMisses(GameState& state, int count, ImVec2 anchor = ImVec2(-1.0f, -1.0f)){
    if (count == 0) return;
    logCheck(TextFormat("%9.3f  missed  %d %s", checkSongTime, count, count == 1 ? "note" : "notes"));
    feedbackMiss(feedback, state.combo, anchor);
    state.combo = 0;
    state.multiplier = 1;
    state.missCount += count;
    state.rhythm = 0.0f;
}

static void soundHit(const JudgeResult& result, bool fromKeys); // further down: it needs the song's notes

// Scoring is the game's own rule on top of judging: perfect hits build the multiplier, near hits don't
static void scoreHit(GameState& state, const JudgeResult& result, ImVec2 anchor){
    const Judgement judgement = result.judgement;
    const int notesHit = result.notesHit;
    const double error = result.error;
    logCheck(TextFormat("%9.3f  hit     %s %s %+.0f ms", checkSongTime, pitchText(result.pitch).c_str(),
                        judgement == Judgement::Perfect ? "perfect" : "good", error * 1000.0));
    state.errorsMs.push_back((float)(error * 1000.0));
    for (int i = 0; i < notesHit; i++){
        state.combo++;
        if (judgement == Judgement::Perfect){
            state.multiplier = std::min(state.multiplier + 1, MAX_MULTIPLIER);
            state.score += 100 * state.multiplier;
            state.rhythm = std::min(1.0f, state.rhythm + RHYTHM_FILL_PER_PERFECT);
            state.perfectCount++;
        } else {
            state.score += 10 * state.multiplier;
            state.rhythm *= 0.5f;
            state.nearCount++;
        }
    }
    state.maxCombo = std::max(state.maxCombo, state.combo);
    feedbackHit(feedback, judgement, error, state.combo, anchor);
    if (instrumentHitSounds) soundHit(result, false);
}

// Number keys 1 to 8 stand for the strings, lowest first
static void handleKeyboard(std::vector<PlayNote>& notes, GameState& state, float songTime, int laneCount, bool hitSounds){
    for (int lane = 0; lane < laneCount; lane++){
        if (!IsKeyPressed(laneKeys[lane])) continue;
        PlayerInput press;
        press.time = songTime;
        press.stringIndex = lane;
        JudgeResult result = judgeInput(notes, press);
        if (result.judgement == Judgement::Ignored) continue;
        scoreHit(state, result, judgementAnchor(notes, result.noteIndex));
        if (hitSounds) soundHit(result, true); // the key plays the note it hit
    }
}

// The notes due from just before now to a little ahead: the lowest of them tells the note detector how low it must
// look, so a pitch is known in two of that note's periods instead of two of the instrument's lowest (core/notedetector)
const float HINT_BEHIND_S = 0.25f; // wider than the judging window: a late note is still looked for
const double SLIDE_INTO_S = 0.3;   // a slide into a note reaches it this long after its pluck, at most
static double unmatchedPluckAt = -100.0; // the last pluck that matched no note: a slide from it may reach one
const double MISS_WAIT_S = 0.4;    // the longest a note's miss waits for something heard to be known (a held hammer-on: 0.25 s)
const float HINT_AHEAD_S = 0.6f;

// How far a note played was off the note due nearest it in pitch, around when it was played: the instrument's
// tuning, as far as one note can tell (core/tuningcheck looks at several)
static void watchNoteTuning(TuningWatch& watch, const std::vector<PlayNote>& notes, const PlayedNote& played, float time, bool anyOctave){
    const float heard = played.pitch + played.cents / 100.0f;
    float nearest = 1e9f;
    auto due = std::lower_bound(notes.begin(), notes.end(), time - NEAR_WINDOW_S, [](const PlayNote& note, float t){ return note.time < t; });
    for (; due != notes.end() && due->time <= time + NEAR_WINDOW_S; ++due){
        float off = anyOctave ? centsOff(heard, due->pitch) : (heard - due->pitch) * 100.0f;
        if (std::fabs(off) < std::fabs(nearest)) nearest = off;
    }
    if (nearest < 1e8f) watchTuning(watch, nearest);
}

// Chords: a single-note detector hears two notes plucked together as a muddle (a fifth reads as the note an octave
// under its root), so when a chord is due, each pluck near it is checked for the chord's notes in the sound itself
// (core/chords). Its judgement waits for enough of the sound, but is timed at the pluck.
struct ChordListening {
    struct Pluck {
        long long start; // in the samples heard so far
        float time;      // song time
    };
    std::vector<float> sound;  // the latest second from the instrument
    long long soundEnd = 0;    // samples heard so far: where `sound` ends
    std::vector<Pluck> plucks; // waiting for enough sound after them
    float lastChordAt = -100.0f; // song time of the last chord heard this way
};
const float VIDEO_WASH = 0.72f;   // how much of the background's color is laid over a song's video
const float SAME_PLUCK_S = 0.06f; // a note the detector finds this close to a chord heard is that chord's pluck

// The unjudged chord (two notes or more at one time) due nearest `time`, within the near window: its notes' indices
static std::vector<int> chordDueAt(const std::vector<PlayNote>& notes, float time){
    std::vector<int> chord;
    auto it = std::lower_bound(notes.begin(), notes.end(), time - NEAR_WINDOW_S, [](const PlayNote& note, float t){ return note.time < t; });
    for (; it != notes.end() && it->time <= time + NEAR_WINDOW_S; ++it){
        if (it->judged) continue;
        std::vector<int> here;
        for (auto same = it; same != notes.end() && same->time - it->time < 0.001f; ++same) if (!same->judged) here.push_back((int)(same - notes.begin()));
        bool nearer = chord.empty() || std::fabs(it->time - time) < std::fabs(notes[chord[0]].time - time);
        if (here.size() >= 2 && nearer) chord = here;
        it += here.size() - 1;
    }
    return chord;
}

static void listenForChords(ChordListening& listening, std::vector<PlayNote>& notes, GameState& state, float songTime, float inputOffset){
    const std::vector<float>& fresh = latestInputSamples();
    const int rate = noteInputSampleRate();
    if (rate <= 0) return;
    listening.sound.insert(listening.sound.end(), fresh.begin(), fresh.end());
    listening.soundEnd += (long long)fresh.size();
    if ((int)listening.sound.size() > rate) listening.sound.erase(listening.sound.begin(), listening.sound.end() - rate);
    for (double age : noteInputAttacks()){
        float time = (float)heardAt(songTime, age, inputOffset);
        if (chordDueAt(notes, time).empty()) continue;
        listening.plucks.push_back({ listening.soundEnd - (long long)(age * rate), time });
    }
    const long long length = (long long)(CHORD_LISTEN_S * rate), soundStart = listening.soundEnd - (long long)listening.sound.size();
    for (size_t i = 0; i < listening.plucks.size();){
        const ChordListening::Pluck pluck = listening.plucks[i];
        if (pluck.start + length > listening.soundEnd){ i++; continue; } // not all of it heard yet
        listening.plucks.erase(listening.plucks.begin() + i);
        std::vector<int> chord = chordDueAt(notes, pluck.time);
        if (chord.empty() || pluck.start < soundStart) continue; // the detector heard it first, or it's gone by
        std::vector<int> pitches;
        for (int index : chord) pitches.push_back(notes[index].pitch);
        if (!soundHoldsNotes(listening.sound.data() + (pluck.start - soundStart), (int)length, rate, pitches)) continue;
        PlayerInput input;
        input.time = pluck.time;
        input.pitch = pitches[0]; // one of its notes completes the chord
        JudgeResult result = judgeInput(notes, input);
        if (result.judgement == Judgement::Ignored) continue;
        scoreHit(state, result, judgementAnchor(notes, result.noteIndex));
        listening.lastChordAt = pluck.time;
    }
}

// With an instrument: each played note is placed in song time (now, minus how long ago it started, minus the
// input device's delay) and judged by its pitch. Rhythm mode needs no pitch: each attack is judged the moment it's
// heard. Either way every attack is noted, for the hit line's flash.
static void handleInstrument(std::vector<PlayNote>& notes, GameState& state, float songTime, float inputOffset,
                             std::vector<int>& lastPlayed, bool rhythmMode, double& lastAttackAt, bool anyOctave, TuningWatch* tuning,
                             ChordListening& chords){
    // The note due nearest now, for the synth heard in place of the instrument to start at the pluck
    const PlayNote* nearest = nullptr;
    int lowestDue = -1;
    auto due = std::lower_bound(notes.begin(), notes.end(), songTime - HINT_BEHIND_S,
                                [](const PlayNote& note, float time){ return note.time < time; }); // sorted by time
    for (; due != notes.end() && due->time <= songTime + HINT_AHEAD_S; ++due){
        if (!due->judged && (lowestDue < 0 || due->pitch < lowestDue)) lowestDue = due->pitch;
        if (!due->judged && (!nearest || std::fabs(due->time - songTime) < std::fabs(nearest->time - songTime))) nearest = &*due;
    }
    expectSynthNote(nearest && !anyOctave && std::fabs(nearest->time - songTime) <= NEAR_WINDOW_S ? nearest->pitch : -1);
    // On another instrument the octave played isn't known: no hint, the detector looks down to the instrument's lowest
    expectLowestNote(lowestDue >= 0 && !anyOctave ? midiToFrequency((float)lowestDue) : 0.0f);

    const std::vector<PlayedNote>& played = updateNoteInput();
    // On the part's own instrument, where the chord's notes are known to the octave
    if (!rhythmMode && !anyOctave) listenForChords(chords, notes, state, songTime, inputOffset);
    for (double age : noteInputAttacks()){
        lastAttackAt = GetTime() - age;
        if (!rhythmMode) continue;
        PlayerInput input;
        input.time = heardAt(songTime, age, inputOffset);
        input.anyNote = true;
        JudgeResult result = judgeInput(notes, input);
        if (result.judgement != Judgement::Ignored) scoreHit(state, result, judgementAnchor(notes, result.noteIndex));
    }
    for (const PlayedNote& note : played){
        lastPlayed = { note.pitch };
        logHeard(heardAt(songTime, note.age, inputOffset), note.pitch);
        if (rhythmMode) continue; // judged at its attack, above
        if (tuning) watchNoteTuning(*tuning, notes, note, (float)heardAt(songTime, note.age, inputOffset), anyOctave);
        PlayerInput input;
        input.time = heardAt(songTime, note.age, inputOffset);
        if (std::fabs((float)input.time - chords.lastChordAt) < SAME_PLUCK_S) continue; // that chord's own pluck, counted
        input.pitch = note.pitch;
        input.anyOctave = anyOctave;
        JudgeResult result = judgeInput(notes, input);
        // Slid into: a pluck a fret or two off, matching nothing, then the slide (or hammer-on) to the note written.
        // The note was played at the pluck.
        if (result.judgement == Judgement::Ignored && note.legato && input.time - unmatchedPluckAt <= SLIDE_INTO_S){
            input.time = unmatchedPluckAt;
            result = judgeInput(notes, input);
        }
        if (result.judgement != Judgement::Ignored) scoreHit(state, result, judgementAnchor(notes, result.noteIndex));
        else if (!note.legato) unmatchedPluckAt = input.time;
    }
    // A slide still held back by the detector (a pluck may follow it: core/notedetector), from a pluck that matched
    // nothing, onto a note due then: that note, played at the pluck, now
    int slidPitch;
    double slidAge;
    if (!rhythmMode && noteInputHeldChange(slidPitch, slidAge)){
        const double slidAt = heardAt(songTime, slidAge, inputOffset);
        if (slidAt > unmatchedPluckAt && slidAt - unmatchedPluckAt <= SLIDE_INTO_S){
            PlayerInput input;
            input.time = unmatchedPluckAt;
            input.pitch = slidPitch;
            input.anyOctave = anyOctave;
            JudgeResult result = judgeInput(notes, input);
            if (result.judgement != Judgement::Ignored){
                scoreHit(state, result, judgementAnchor(notes, result.noteIndex));
                logHeard(slidAt, slidPitch);
                unmatchedPluckAt = -100.0;
            }
        }
    }
    // Strings plucked together, heard unasked (core/polyphony): shown in full, and a chord due that listenForChords
    // didn't find in the sound is still played if these are its notes
    for (const PlayedChord& chord : noteInputChords()){
        lastPlayed = chord.pitches;
        for (int pitch : chord.pitches) logHeard(heardAt(songTime, chord.age, inputOffset), pitch);
        if (rhythmMode || anyOctave) continue;
        PlayerInput input;
        input.time = heardAt(songTime, chord.age, inputOffset);
        if (std::fabs((float)input.time - chords.lastChordAt) < SAME_PLUCK_S) continue; // counted already
        std::vector<int> dueChord = chordDueAt(notes, (float)input.time);
        bool held = !dueChord.empty() && std::all_of(dueChord.begin(), dueChord.end(), [&](int index){
            return std::count(chord.pitches.begin(), chord.pitches.end(), notes[index].pitch) > 0;
        });
        if (!held) continue;
        input.pitch = notes[dueChord[0]].pitch; // one of its notes completes the chord
        JudgeResult result = judgeInput(notes, input);
        if (result.judgement == Judgement::Ignored) continue;
        scoreHit(state, result, judgementAnchor(notes, result.noteIndex));
        chords.lastChordAt = (float)input.time;
    }
}

// Everything the play screen needs while a song is running
static struct {
    Chart chart;
    std::vector<PlayNote> notes;
    Score score;                 // the track written down: bars, note values, rests (for the sheet music and tab)
    GameState state;
    GameplayOptions options;
    float songTime = 0.0f;
    std::vector<int> lastPlayed; // the latest note heard from the instrument (the notes, plucked together), shown so
                                 // the player can trust the input
    double lastAttackAt = -10.0; // when the instrument was last plucked (GetTime): the hit line flashes with it
    bool anyOctave = false;      // the part is played on another instrument than its own: notes count in any octave
    float hitLineX = 180.0f;  // where the views put the hit line, for the judgements drawn at it
    bool paused = false;
    bool keys = false;            // playing a keys part, on a MIDI keyboard
    std::string partName;
    Rectangle distribution = {}; // where the HUD's timing distribution is: the results screen grows it from there
    float resumeAt = -1.0f;   // after a resume: the song time it was paused at (GET READY shows until then)
    float countInBeat = 0.0f; // from the top: the count-in's beat (seconds), before the song's start; 0 for none
    int countInBeats = 0;
    float startsAt = 0.0f;    // where in the audio the song starts: 0, or a trimmed song's start
    float endsAt = 0.0f;      // where a trimmed song ends, 0 for the audio's own end
    std::string fingerprint;  // of the part being played
    ChordListening chords;    // plucks near a chord, checked for its notes
    TuningWatch tuning;       // the notes played, for the instrument going out of tune
    bool watchingTuning = false; // with a guitar or a bass, until the player chooses to play on out of tune
    bool outOfTune = false;   // paused for it
    float outOfTuneCents = 0.0f;
    // Practising (options.practice): the section's notes as they are before a pass, where it starts and ends, how
    // it's going; and, note by note, the note the song is waiting on
    std::vector<PlayNote> sectionNotes;
    float sectionStart = 0.0f, sectionEnd = 0.0f;
    PracticeProgress progress;
    bool waiting = false;
    float waitTime = 0.0f;
    bool active = false;
} game;

// The feedback for a hit: the note itself, as long as it's written to ring, a chord all of its notes, on the sound the
// song gives the part (the editor's choice: its own instrument unless it says otherwise, as the editor plays it).
// Or else, if that's the player's setting or the song's, a drop; or nothing, if the song says so.
const float MIN_HIT_NOTE_S = 0.2f, MAX_HIT_NOTE_S = 1.5f;

static void soundHit(const JudgeResult& result, bool fromKeys){
    const GameplayOptions& options = game.options;
    const std::string& sound = game.chart.frettedTracks[0].hitSound;
    if (sound == "none") return;
    if (!options.hitSoundIsNote || sound == "drop" || game.keys || result.noteIndex < 0 || result.noteIndex >= (int)game.notes.size()){
        if (fromKeys) playPreview(midiToFrequency((float)result.pitch));
        else playHitSound(result.judgement == Judgement::Perfect);
        return;
    }
    const float volume = fromKeys ? options.keyVolume : options.hitSoundVolume;
    if (volume <= 0.0f) return;
    const bool onString = sound.empty() || sound == "bass" || sound == "guitar";
    const bool bass = sound.empty() ? game.chart.frettedTracks[0].type == InstrumentType::Bass : sound == "bass";
    const PlayNote& first = game.notes[result.noteIndex];
    for (size_t i = result.noteIndex; i < game.notes.size() && game.notes[i].time - first.time < 0.001f; i++){
        const PlayNote& note = game.notes[i];
        if (!note.hit || (i != (size_t)result.noteIndex && result.notesHit <= 1)) continue; // a chord's notes, when they all came
        float rings = note.writtenLength > 0.0f ? note.writtenLength : note.length;
        if (onString) playStringNote(midiToFrequency((float)note.pitch), bass, std::clamp(rings, MIN_HIT_NOTE_S, MAX_HIT_NOTE_S) / songSpeed(), volume);
        else playBuiltInNote(sound.c_str(), midiToFrequency((float)note.pitch), volume);
    }
}

// Rhythm mode on the keyboard, as in taiko: F and J hit dons, D and K hit kas, and each sounds its drum
static void handleDrumKeys(){
    static const int KEYS[4] = { KEY_F, KEY_J, KEY_D, KEY_K };
    for (int k = 0; k < 4; k++){
        if (!IsKeyPressed(KEYS[k])) continue;
        bool ka = k >= 2;
        playDrum(ka);
        PlayerInput press;
        press.time = game.songTime;
        press.stringIndex = ka ? 1 : 0;
        JudgeResult result = judgeInput(game.notes, press);
        if (result.judgement != Judgement::Ignored) scoreHit(game.state, result, judgementAnchor(game.notes, result.noteIndex));
    }
}

// From the top, a bar is counted in (two for a fast song), clicking on each beat with the count on screen: a note on
// the very first beat isn't a surprise, and the first notes' rings are already closing in while it counts. The song
// is started that long before its start (its clock counts up from below it): the audio's own, or where it's trimmed to.
static void startWithCountIn(float from){
    const int startTick = std::max(0, (int)std::lround(secondsToTick(game.chart, from)));
    const TimeSignatureChange& time = timeSignatureAt(game.chart, startTick);
    const double bar = tickToSeconds(game.chart, startTick + ticksPerBar(game.chart, time)) - tickToSeconds(game.chart, startTick);
    const int bars = bar < MIN_COUNT_IN_S ? 2 : 1;
    const double countIn = bar * bars, beat = bar / std::max(1, time.beats);
    double begins = playSongFrom(from - countIn, game.startsAt); // on the engine's clock, when the count starts
    if (begins < 0.0){
        playSong(false); // a song that can't be started ahead (read as it plays): from the top at once
        return;
    }
    game.countInBeat = (float)beat;
    game.countInBeats = time.beats * bars;
    for (int k = 0; k < game.countInBeats; k++) playClickAt(begins + k * beat / songSpeed(), k % time.beats == 0); // the song's beats, at its speed
}

bool startGameplay(const std::string& chartPath, const GameplayOptions& options, std::string& error){
    Chart chart;
    if (!loadChart(chartPath, chart, error)) return false;
    if (chart.audioFile.empty()){
        error = chartPath + ": chart has no 'audio' line";
        return false;
    }
    // The audio file is named relative to the chart's own folder
    std::filesystem::path audioPath = std::filesystem::path(chartPath).parent_path() / chart.audioFile;
    return startGameplayWithChart(chart, audioPath.string(), options, 0, error);
}

bool startGameplayWithChart(const Chart& chart, const std::string& audioPath, const GameplayOptions& options, int fromTick,
                            std::string& error){
    stopGameplay();
    checkSongAudio = audioPath;
    unmatchedPluckAt = -100.0;
    if (options.practice.on) fromTick = options.practice.fromTick; // practising: its section only
    if (options.part < 0 || options.part >= partCount(chart)){
        error = "the chart has no part " + std::to_string(options.part + 1) + " to play";
        return false;
    }
    // Only the part being played is kept: everything below works on the chart's first track. A keys part becomes a
    // one-string instrument whose frets are its pitches: judging, scoring and the sheet music then work as for
    // any part.
    game.fingerprint = partFingerprint(chart, options.part); // of the whole part, before anything is left out
    if (options.rhythmMode) game.fingerprint += "-rhythm";   // rhythm runs have records of their own
    game.partName = partName(chart, options.part);
    game.keys = isKeysPart(chart, options.part);
    game.chart = chart;
    if (game.keys){
        const KeysTrack& keys = chart.keysTracks[options.part - chart.frettedTracks.size()];
        FrettedTrack asOneString;
        asOneString.type = InstrumentType::Keys;
        asOneString.name = keys.name;
        asOneString.tuning = {0};
        for (const KeysNote& note : keys.notes) asOneString.notes.push_back({note.tick, 0, note.pitch, note.duration});
        game.chart.frettedTracks = { asOneString };
    } else {
        game.chart.frettedTracks = { chart.frettedTracks[options.part] };
    }
    game.chart.keysTracks.clear();
    // A trimmed song: only the part of the audio that's kept is played, and only its notes
    dropTrimmedNotes(game.chart);
    game.startsAt = (float)game.chart.trimStart;
    game.endsAt = (float)game.chart.trimEnd;
    // Starting part-way: the notes before are left out, so the score (the sheet music) is built without them too
    std::vector<FrettedNote>& chartNotes = game.chart.frettedTracks[0].notes;
    chartNotes.erase(chartNotes.begin(), std::lower_bound(chartNotes.begin(), chartNotes.end(), fromTick,
                     [](const FrettedNote& note, int tick){ return note.tick < tick; }));
    if (options.practice.on){
        chartNotes.erase(std::lower_bound(chartNotes.begin(), chartNotes.end(), options.practice.toTick,
                         [](const FrettedNote& note, int tick){ return note.tick < tick; }), chartNotes.end());
    }
    const FrettedTrack& track = game.chart.frettedTracks[0];
    if (!game.keys && (int)track.tuning.size() > MAX_LANES){
        error = "track '" + track.name + "' has " + std::to_string(track.tuning.size())
                + " strings, the prototype supports up to " + std::to_string(MAX_LANES);
        return false;
    }

    keepSongStretched(options.practice.on && options.practice.noteByNote); // its speed changes as it plays
    setSongSpeed(options.practice.on ? options.practice.speed : 1.0f); // before loading: it's opened to be played so
    if (!loadSong(audioPath, error)) return false;
    // Its video, for behind the notes. A song plays the same without one: a video that's missing (a package made
    // without it) or can't be read is only noted.
    closeSongVideo();
    if (options.video && !chart.videoFile.empty()){
        std::string videoError;
        std::string videoPath = (std::filesystem::path(audioPath).parent_path() / chart.videoFile).string();
        if (!openSongVideo(videoPath, videoError)) TraceLog(LOG_INFO, "Song video: %s", videoError.c_str());
    }

    // Gameplay notes: the chart's notes converted to seconds, plus per-run judging state
    game.notes.clear();
    game.notes.reserve(track.notes.size());
    for (const FrettedNote& chartNote : track.notes){
        int pitch = track.tuning[chartNote.stringIndex] + chartNote.fret;
        float time = (float)tickToSeconds(game.chart, chartNote.tick);
        PlayNote note{time, chartNote.stringIndex, chartNote.fret, pitch};
        note.length = chartNote.duration > 0 ? (float)tickToSeconds(game.chart, chartNote.tick + chartNote.duration) - time : 0.0f;
        game.notes.push_back(note);
    }
    game.score = buildScore(game.chart, track); // its events point into track.notes, in the same order as game.notes
    // Each note's written length, as the sheet music shows it (a note tied on adds the next value): how many beats it
    // should ring, which the neck's rings count
    bool continuing = false; // the event carries on a tie from the one before: its notes were counted there
    for (size_t e = 0; e < game.score.events.size(); e++){
        const ScoreEvent& event = game.score.events[e];
        bool tied = event.tiedToNext;
        if (!event.rest && event.firstNote >= 0 && !continuing){
            int ticks = event.length;
            for (size_t next = e; game.score.events[next].tiedToNext && next + 1 < game.score.events.size(); next++){
                ticks += game.score.events[next + 1].length;
            }
            for (int i = 0; i < event.noteCount && event.firstNote + i < (int)game.notes.size(); i++){
                PlayNote& note = game.notes[event.firstNote + i];
                note.beats = (float)ticks / game.chart.resolution;
                note.writtenLength = (float)tickToSeconds(game.chart, event.tick + ticks) - note.time;
            }
        }
        continuing = tied;
    }
    if (options.rhythmMode){
        // Rhythm mode: the part's hits instead of its notes, a note's string its kind (0 don, 1 ka), its fret 1 if
        // it's big. The score stays for its bar lines.
        game.notes.clear();
        for (const RhythmHit& hit : rhythmHits(chart, options.part)){
            if (hit.tick < fromTick) continue;
            double at = tickToSeconds(game.chart, hit.tick);
            if (at < game.startsAt || (game.endsAt > 0.0f && at >= game.endsAt)) continue; // trimmed away
            game.notes.push_back({(float)tickToSeconds(game.chart, hit.tick), hit.kind == RhythmHitKind::Ka ? 1 : 0, hit.big ? 1 : 0, -1});
        }
    }
    game.state = {};
    feedback = {};
    game.paused = false;
    game.resumeAt = -1.0f;
    game.state.multiplier = 1;
    game.options = options;
    game.lastPlayed.clear();
    heardLog.clear();
    game.anyOctave = false; // set below when the part is played on another instrument than its own
    // A keys part is played on a MIDI keyboard if one is connected, else on the computer keyboard, laid out from
    // the C at or below the part's lowest note
    if (game.keys && !options.rhythmMode){
        std::string midiError;
        if (!startMidiInput(options.midiDevice, midiError)){
            int lowest = 127;
            for (const PlayNote& note : game.notes) lowest = std::min(lowest, note.pitch);
            startPianoKeys(options.pianoKeys, pianoBaseFor(game.notes.empty() ? 60 : lowest));
        }
    }
    if (options.rhythmMode && game.keys){
        std::string midiError;
        startMidiInput(options.midiDevice, midiError); // any key on it counts; without one, the drum keys do
    }
    instrumentHitSounds = options.playWithInstrument && !game.keys;
    if (options.playWithInstrument && !game.keys){
        // The instrument played, on its own input. On the part's own instrument, listen down to just below the part's
        // lowest string (a drop tuning gets its range); on another (a guitar melody on a bass), down to that instrument's
        // low E, and the notes count in any octave.
        const bool bassPlayed = options.instrument == InputRole::Bass;
        game.anyOctave = bassPlayed != (track.type == InstrumentType::Bass);
        int lowestPitch = game.anyOctave ? (bassPlayed ? 28 : 40) : *std::min_element(track.tuning.begin(), track.tuning.end());
        float lowest = midiToFrequency((float)lowestPitch) * 0.9f;
        int channel = bassPlayed ? options.bassChannel : options.guitarChannel;
        if (!startNoteInput(options.inputDevice, lowest, error, channel)){
            error = "Playing with your instrument: " + error + " (see Settings, Instruments)";
            unloadSong();
            return false;
        }
        if (recordChecks) beginCheck();
    }
    game.songTime = 0.0f;
    game.tuning = {};
    game.chords = {};
    game.watchingTuning = options.playWithInstrument && !game.keys && !options.rhythmMode;
    game.outOfTune = false;
    game.active = true;
    game.countInBeats = 0;
    game.waiting = false;
    setSongVolume(1.0f);
    if (options.practice.on){
        game.sectionNotes = game.notes;
        game.sectionStart = (float)tickToSeconds(game.chart, options.practice.fromTick);
        game.sectionEnd = (float)tickToSeconds(game.chart, options.practice.toTick);
        game.progress = PracticeProgress{};
        game.progress.speed = options.practice.speed;
        startWithCountIn(game.sectionStart);
    }
    else if (fromTick > 0) playSongFrom(tickToSeconds(game.chart, fromTick) - LEAD_IN_S, game.startsAt);
    else startWithCountIn(game.startsAt);
    return true;
}

// Practising: the section again from the top, its notes as new, counted in at the speed it's at
static void startPass(){
    game.notes = game.sectionNotes;
    game.state = {};
    game.state.multiplier = 1;
    game.chords = {};
    game.waiting = false;
    startWithCountIn(game.sectionStart);
}

// A pass over: how much of it was played; a pass with every note played moves the tempo up (gradually) or masters
// the section. False when the practice is over: mastered, or as many passes as were asked for.
static bool endPass(){
    const PracticeOptions& practice = game.options.practice;
    PracticeProgress& progress = game.progress;
    int hits = 0;
    for (const PlayNote& note : game.notes) hits += note.hit;
    const int total = (int)game.notes.size();
    progress.passes++;
    progress.lastAccuracy = total > 0 ? (float)hits / total : 1.0f;
    progress.bestAccuracy = std::max(progress.bestAccuracy, progress.lastAccuracy);
    if (hits == total){
        if (practice.gradual && progress.speed < 0.999f){
            progress.speed = std::min(1.0f, std::round((progress.speed + practice.step) * 100.0f) / 100.0f);
            progress.raisedAt = GetTime();
        } else {
            progress.mastered = true;
        }
    }
    stopSong();
    if ((practice.passes > 0 && progress.passes >= practice.passes) || (practice.passes == 0 && progress.mastered)) return false;
    setSongSpeed(progress.speed);
    startPass();
    return true;
}

// Practising strictly: a miss, or any hit short of perfect, and the pass starts over at once (it isn't counted)
static bool passFailed(){
    using StartOver = PracticeOptions::StartOver;
    const StartOver strict = game.options.practice.startOver;
    return (strict != StartOver::Never && game.state.missCount > 0) ||
           (strict == StartOver::UnlessPerfect && game.state.nearCount > 0);
}
static void startPassOver(){
    stopSong();
    game.progress.startedOver++;
    game.progress.startedOverAt = GetTime();
    setSongSpeed(game.progress.speed);
    startPass();
}

void pauseGameplay(){
    if (!game.active || game.paused) return;
    stopSong();
    stopPreviews();
    game.paused = true;
}

void resumeGameplay(){
    if (!game.active || !game.paused) return;
    if (game.outOfTune) game.watchingTuning = false; // played on out of tune, by choice: not asked again this run
    game.outOfTune = false;
    game.paused = false;
    game.resumeAt = game.songTime;
    playSongFrom(game.songTime + game.options.offsetSeconds * songSpeed() - RESUME_RUNUP_S, game.startsAt);
}

bool gameplayPaused(){
    return game.active && game.paused;
}

bool gameplayOutOfTune(float& cents){
    cents = game.outOfTuneCents;
    return game.active && game.paused && game.outOfTune;
}

bool updateGameplay(){
    if (!game.active) return false;
    // Away from the game (another window has the focus), it waits
    if (!IsWindowFocused()) pauseGameplay();
    if (game.paused){
        if (noteInputActive()) updateNoteInput(); // what's played while paused is thrown away, not judged later
        if (midiInputActive()) updateMidiInput();
        if (pianoKeysActive()) updatePianoKeys();
        return true;
    }

    // The song's playback position is the clock: notes stay in sync with the music even if frames stutter
    // The offset shifts the whole game against the audio: if sound reaches your ears late (Bluetooth,
    // slow drivers), a positive offset moves notes and judging later to match what you hear
    // Note by note: the song slows into each note still to play, never quite stopping, until it's played
    float speed = songSpeed();
    game.songTime = (float)(songPosition() - game.options.offsetSeconds * speed);
    game.waiting = false;
    if (game.options.practice.on && game.options.practice.noteByNote){
        const float tempo = game.progress.speed;
        auto due = std::find_if(game.notes.begin(), game.notes.end(), [](const PlayNote& note){ return !note.judged; });
        float target = tempo;
        if (due != game.notes.end()){
            const float until = due->time - game.songTime;
            target = tempo * std::clamp(until / SLOW_FROM_S, CRAWL, 1.0f);
            game.waiting = until <= SLOW_FROM_S * CRAWL; // down to the crawl: waiting on it
            game.waitTime = due->time;
            // The sound crawls on past the note; the notes on screen, and a key pressed, stay on it
            if (game.waiting) game.songTime = std::min(game.songTime, due->time);
        }
        // Slowing follows the note at once; speeding up again eases in
        float next = target < speed ? target : speed + (target - speed) * std::min(1.0f, GetFrameTime() / EASE_UP_S);
        if (std::fabs(next - speed) > 1e-4f){
            setSongSpeedLive(next);
            speed = next;
        }
    }
    inputScale = speed;
    frozenTime = game.waiting;
    frozenAt = game.waitTime;
    checkSongTime = game.songTime;
    // F9: recording a check, or not
    if (IsKeyPressed(KEY_F9) && noteInputActive()){
        recordChecks = !recordChecks;
        if (recordChecks) beginCheck();
        else saveCheck();
    }

    for (PlayNote& note : game.notes){
        if (note.hitFlash > 0.0f) note.hitFlash -= GetFrameTime();
    }
    const FrettedTrack& track = game.chart.frettedTracks[0];
    if (game.options.rhythmMode) handleDrumKeys();
    else handleKeyboard(game.notes, game.state, game.songTime, (int)track.tuning.size(), game.options.hitSounds);
    if (noteInputActive()) handleInstrument(game.notes, game.state, game.songTime, game.options.inputOffsetSeconds, game.lastPlayed,
                                            game.options.rhythmMode, game.lastAttackAt, game.anyOctave,
                                            game.watchingTuning ? &game.tuning : nullptr, game.chords);
    // The two clocks side by side, four times a second: the recording's end is what was read this frame
    if (inputRecording() && (lastSync < 0.0 || inputRecordingSeconds() - lastSync >= 0.25)){
        lastSync = inputRecordingSeconds();
        checkLog.push_back(TextFormat("sync %.3f %.3f", lastSync, game.songTime));
        if (firstSyncRecording < 0.0){
            firstSyncRecording = lastSync;
            firstSyncSong = game.songTime;
        }
    }
    // Out of tune, the notes can't be played right: stop, so the player can tune rather than fight it
    if (game.watchingTuning && looksOutOfTune(game.tuning, game.outOfTuneCents)){
        game.outOfTune = true;
        pauseGameplay();
        return true;
    }
    if (midiInputActive() || pianoKeysActive()){
        // Each key on its own: pressing one note of a chord doesn't play the rest. Every key sounds, hit or not:
        // it's an instrument being played.
        for (const PlayedNote& played : midiInputActive() ? updateMidiInput() : updatePianoKeys()){
            playKeysNote(midiToFrequency((float)played.pitch));
            PlayerInput input;
            input.time = heardAt(game.songTime, played.age, 0.0f);
            input.pitch = played.pitch;
            input.completesChord = false;
            input.anyNote = game.options.rhythmMode;
            JudgeResult result = judgeInput(game.notes, input);
            if (result.judgement != Judgement::Ignored) scoreHit(game.state, result, judgementAnchor(game.notes, result.noteIndex));
            game.lastPlayed = { played.pitch };
            logHeard(input.time, played.pitch);
        }
    }
    if (!(game.options.practice.on && game.options.practice.noteByNote)){ // note by note, nothing goes by unplayed
        // Something heard and still being listened to (a pluck's pitch, a hammer-on held back) may be the note
        // due: the notes it could be are only missed once it's known. Up to MISS_WAIT_S.
        int missedNote = -1;
        float missedBy = game.songTime;
        const double pending = noteInputActive() ? noteInputPendingAge() : -1.0;
        if (pending >= 0.0 && pending < MISS_WAIT_S){
            const float heardAt = (float)(game.songTime - (pending + game.options.inputOffsetSeconds) * inputScale);
            missedBy = std::min(missedBy, heardAt); // missed: notes too early for it to be them (judging window before it)
            // A slide from a pluck that matched nothing would be judged at that pluck
            if (heardAt - unmatchedPluckAt <= SLIDE_INTO_S) missedBy = std::min(missedBy, (float)unmatchedPluckAt);
        }
        // A pluck that matched nothing may be sliding onto a note: the notes it could reach wait for it
        if (game.songTime - unmatchedPluckAt <= SLIDE_INTO_S * inputScale) missedBy = std::min(missedBy, (float)unmatchedPluckAt);
        int missed = markMisses(game.notes, missedBy, &missedNote);
        scoreMisses(game.state, missed, judgementAnchor(game.notes, missedNote));
    }

    // Practising: at the section's end, the pass is counted and the next begins (or the practice is over)
    if (game.options.practice.on){
        if (passFailed()){
            startPassOver();
            return true;
        }
        if (game.songTime < game.sectionEnd + PASS_TAIL_S && !songEnded()) return true;
        scoreMisses(game.state, markMisses(game.notes, game.songTime + 1e9));
        return endPass();
    }

    // A trimmed song fades out into its end, and ends there
    bool trimmedEnd = false;
    if (game.endsAt > 0.0f){
        double left = game.endsAt - (game.songTime + game.options.offsetSeconds * speed); // against the audio's own time
        setSongVolume((float)std::clamp(left / TRIM_FADE_S, 0.0, 1.0));
        trimmedEnd = left <= 0.0;
    }
    if (songEnded() || trimmedEnd){
        stopSong();
        // Anything still unjudged when the music stops counts as missed
        scoreMisses(game.state, markMisses(game.notes, game.songTime + 1e9));
        return false;
    }
    return true;
}

// The instrument's pluck lights the hit line the moment it's heard: the judgement follows once its pitch is known,
// a few hundredths of a second later, but the player sees the pluck land at once
const float ATTACK_FLASH_S = 0.15f;

static void drawAttackFlash(Rectangle area, float x){
    float t = (float)(GetTime() - game.lastAttackAt);
    if (t < 0.0f || t > ATTACK_FLASH_S) return;
    float fade = 1.0f - t / ATTACK_FLASH_S;
    fade *= fade; // bright at once, gone softly
    float s = GetScreenHeight() / 720.0f, glow = 28.0f * s;
    Color accent = themeColor(UiColor::Accent);
    DrawRectangleGradientH((int)(x - glow), (int)area.y, (int)glow, (int)area.height, Fade(accent, 0.0f), Fade(accent, 0.3f * fade));
    DrawRectangleGradientH((int)x, (int)area.y, (int)glow, (int)area.height, Fade(accent, 0.3f * fade), Fade(accent, 0.0f));
    DrawRectangleRec({ x - 1.5f * s, area.y, 3.0f * s, area.height }, Fade(accent, 0.9f * fade));
}

void drawGameplay(){
    if (!game.active) return;
    ClearBackground(themeColor(UiColor::Background));
    TimeAxis axis = { game.songTime, (float)HIT_LINE_X, game.options.noteSpeed };
    float width = (float)GetScreenWidth(), height = (float)GetScreenHeight();
    if (songVideoOpen()){
        // The song's video fills the window (cropped where its shape differs), washed toward the background so the
        // notes stay the thing to read. It follows what's heard: the song's time, as the notes do.
        const Texture2D& picture = songVideoTexture(game.songTime + game.chart.videoOffset);
        float scale = std::max(width / picture.width, height / picture.height);
        float shownWidth = width / scale, shownHeight = height / scale;
        DrawTexturePro(picture, { (picture.width - shownWidth) / 2, (picture.height - shownHeight) / 2, shownWidth, shownHeight },
                       { 0, 0, width, height }, { 0, 0 }, 0.0f, WHITE);
        DrawRectangle(0, 0, (int)width, (int)height, Fade(themeColor(UiColor::Background), VIDEO_WASH));
    }
    Rectangle viewsArea = { 0, height * 0.14f, width, height * 0.72f }; // below the HUD, above the combo and timing bar
    if (game.options.rhythmMode){
        drawRhythmLane(viewsArea, game.notes, game.score, axis);
        game.hitLineX = axis.hitLineX;
    } else if (game.keys){
        const bool* down = midiInputActive() ? midiKeysDown() : pianoKeysActive() ? pianoKeysDown() : nullptr;
        game.hitLineX = drawKeysViews(viewsArea, game.options.noteViews, game.notes, game.score, axis, down,
                                      pianoKeysActive() ? pianoKeyFor : nullptr);
    } else {
        game.hitLineX = drawNoteViews(viewsArea, game.options.noteViews, game.notes, game.score, game.chart.frettedTracks[0].tuning,
                                      game.options.lowStringOnTop, axis);
    }
    // Only where there's an upright hit line to light: the rhythm lane. The sheet music (a page), the neck and the piano
    // have none (their "hit line" x is only their middle).
    bool uprightHitLine = game.options.rhythmMode;
    if (noteInputActive() && uprightHitLine) drawAttackFlash(viewsArea, game.hitLineX);
}

void drawGameplayHud(){
    if (!game.active) return;
    ImDrawList* draw = ImGui::GetForegroundDrawList();
    const UiFonts& fonts = uiFonts();
    const GameState& state = game.state;
    const float s = menuScale(), width = ImGui::GetIO().DisplaySize.x;
    const float margin = 28 * s, top = 22 * s;
    auto textWidth = [](ImFont* font, float size, const char* text){
        return font ? font->CalcTextSizeA(size, FLT_MAX, 0.0f, text).x : size * 0.6f * std::strlen(text);
    };

    // Recording a check (F9): a red dot and REC at the top, under the score; then where it was saved, a moment
    if (inputRecording()){
        const float pulse = 0.6f + 0.4f * (float)std::sin(GetTime() * 6.0);
        draw->AddCircleFilled(ImVec2(width * 0.5f - 34 * s, top + 9 * s), 5 * s, uiColor(UiColor::Bad, pulse));
        draw->AddText(fonts.mono, 14 * s, ImVec2(width * 0.5f - 24 * s, top + 1 * s), uiColor(UiColor::Bad), "REC  F9");
    } else if (GetTime() - checkSavedAt < 4.0){
        draw->AddText(fonts.mono, 13 * s, ImVec2(width * 0.5f - textWidth(fonts.mono, 13 * s, checkSaved.c_str()) / 2, top + 1 * s),
                      uiColor(UiColor::Dim), checkSaved.c_str());
    }

    // The rhythm meter: a thin brass line along the top edge, filling as perfect hits keep coming
    draw->AddRectFilled(ImVec2(0, 0), ImVec2(width, 4 * s), uiColor(UiColor::StaffLine));
    draw->AddRectFilled(ImVec2(0, 0), ImVec2(width * state.rhythm, 4 * s), uiColor(UiColor::Accent));

    // The song on the left, with what the instrument is heard playing, so the player can trust the input
    draw->AddText(fonts.bold, 24 * s, ImVec2(margin, top), uiColor(UiColor::Ink), game.chart.title.c_str());
    std::string below = game.chart.artist;
    if (game.options.playWithInstrument){
        std::string heard = game.lastPlayed.empty() ? "Listening..." : "You played";
        for (size_t i = 0; i < game.lastPlayed.size(); i++){
            heard += TextFormat("%s %s%d", i ? " +" : "", pitchClassName(game.lastPlayed[i]), pitchOctave(game.lastPlayed[i]));
        }
        below += below.empty() ? heard : "  ·  " + heard;
    }
    draw->AddText(fonts.text, 16 * s, ImVec2(margin, top + 30 * s), uiColor(UiColor::Dim), below.c_str());
    // Practising: the section, the tempo, the pass, how the last one went; and note by note, the note it waits on
    if (game.options.practice.on){
        const PracticeOptions& practice = game.options.practice;
        const PracticeProgress& progress = game.progress;
        // Where it is: "2" for a bar line, "2.3" for bar 2's third beat
        auto place = [&](int tick){
            const int bar = barNumberAt(game.chart, tick), start = barStartTick(game.chart, bar);
            const int beat = (tick - start) / std::max(1, game.chart.resolution * 4 / timeSignatureAt(game.chart, start).beatUnit);
            return beat == 0 ? std::to_string(bar + 1) : std::string(TextFormat("%d.%d", bar + 1, beat + 1));
        };
        std::string line = "PRACTICE  ·  FROM " + place(practice.fromTick) + " TO " + place(practice.toTick);
        line += TextFormat("  ·  %d%% TEMPO  ·  PASS %d", (int)std::lround(progress.speed * 100.0f), progress.passes + 1);
        if (practice.passes > 0) line += TextFormat(" OF %d", practice.passes);
        if (progress.passes > 0) line += TextFormat("  ·  LAST %d%%", (int)std::lround(progress.lastAccuracy * 100.0f));
        if (progress.startedOver > 0) line += TextFormat("  ·  STARTED OVER %d", progress.startedOver);
        draw->AddText(fonts.mono, 14 * s, ImVec2(margin, top + 56 * s), uiColor(UiColor::Accent), line.c_str());
        if (game.waiting){
            std::string waiting = "WAITING FOR";
            for (const PlayNote& note : game.notes){
                if (std::fabs(note.time - game.waitTime) < 0.001f && !note.hit) waiting += TextFormat("  %s%d", pitchClassName(note.pitch), pitchOctave(note.pitch));
            }
            draw->AddText(fonts.bold, 20 * s, ImVec2(margin, top + 78 * s), uiColor(UiColor::Ink), waiting.c_str());
        }
        // Started over: AGAIN, big, for a moment
        const float sinceOver = (float)(GetTime() - progress.startedOverAt);
        if (sinceOver >= 0.0f && sinceOver < TEMPO_BANNER_S){
            const float fade = 1.0f - sinceOver / TEMPO_BANNER_S;
            const char* banner = "AGAIN";
            const float size = 46 * s * (1.0f + 0.15f * fade * fade);
            draw->AddText(fonts.heavy, size, ImVec2(ImGui::GetIO().DisplaySize.x / 2 - textWidth(fonts.heavy, size, banner) / 2, top + 70 * s),
                          uiColor(UiColor::Bad, std::min(1.0f, fade * 2.0f)), banner);
        }
        // A pass with every note played, and faster now: the new tempo, big, for a moment
        float since = (float)(GetTime() - progress.raisedAt);
        if (since >= 0.0f && since < TEMPO_BANNER_S){
            float fade = 1.0f - since / TEMPO_BANNER_S;
            const char* banner = TextFormat("TEMPO %d%%", (int)std::lround(progress.speed * 100.0f));
            float size = 46 * s * (1.0f + 0.15f * fade * fade);
            draw->AddText(fonts.heavy, size, ImVec2(ImGui::GetIO().DisplaySize.x / 2 - textWidth(fonts.heavy, size, banner) / 2, top + 70 * s),
                          uiColor(UiColor::Accent, std::min(1.0f, fade * 2.0f)), banner);
        }
    }

    // The score on the right, the combo and multiplier under it: the multiplier in brass once it's working
    const char* score = TextFormat("%d", state.score);
    draw->AddText(fonts.heavy, 34 * s, ImVec2(width - margin - textWidth(fonts.heavy, 34 * s, score), top - 4 * s), uiColor(UiColor::Ink), score);
    const char* multiplier = TextFormat("x%d", state.multiplier);
    float multiplierWidth = textWidth(fonts.mono, 15 * s, multiplier);
    draw->AddText(fonts.mono, 15 * s, ImVec2(width - margin - multiplierWidth, top + 38 * s),
                  uiColor(state.multiplier > 1 ? UiColor::Accent : UiColor::Dim), multiplier);

    // The judgements at the hit line, just above the notes; the combo and the timing bar under them, centered
    const float height = ImGui::GetIO().DisplaySize.y;
    // Rhythm mode: which keys hit what, under the lane (or, on an instrument, that any note does)
    if (game.options.rhythmMode){
        bool instrument = noteInputActive() || midiInputActive();
        const char* keys = instrument ? "PLAY ANY NOTE ON THE BEAT" : "F  J   DON        D  K   KA";
        float keysWidth = textWidth(fonts.mono, 14 * s, keys);
        draw->AddText(fonts.mono, 14 * s, ImVec2(width / 2 - keysWidth / 2, ImGui::GetIO().DisplaySize.y * 0.5f + 100 * s),
                      uiColor(UiColor::Dim), keys);
    }
    // The count-in, over where the notes arrive: the beats left, each popping in on its click and settling
    if (game.countInBeats > 0 && game.songTime < game.startsAt && !game.paused){
        float beatsLeft = (game.startsAt - game.songTime) / game.countInBeat;
        int count = std::min(game.countInBeats, (int)std::ceil(beatsLeft - 0.001f));
        float intoBeat = 1.0f - (beatsLeft - std::floor(beatsLeft - 0.001f)); // 0 on the click, towards 1 before the next
        const char* number = TextFormat("%d", std::max(1, count));
        float size = 56 * s * (1.0f + 0.35f * std::max(0.0f, 1.0f - intoBeat * 4.0f));
        float numberWidth = textWidth(fonts.heavy, size, number);
        draw->AddText(fonts.heavy, size, ImVec2(game.hitLineX - numberWidth / 2, ImGui::GetIO().DisplaySize.y * 0.14f - size * 0.75f),
                      uiColor(UiColor::Accent, 1.0f - 0.5f * intoBeat), number);
    }
    // Back from a pause, the song plays its run-up: nothing counts against the player until it's back where it was
    if (game.resumeAt >= 0.0f && game.songTime < game.resumeAt){
        const char* ready = "GET READY";
        float readyWidth = textWidth(fonts.heavy, 26 * s, ready);
        draw->AddText(fonts.heavy, 26 * s, ImVec2(game.hitLineX - readyWidth / 2, ImGui::GetIO().DisplaySize.y * 0.14f - 20 * s),
                      uiColor(UiColor::Accent), ready);
    }
    HitFeedbackLayout layout = { game.hitLineX, height * 0.14f - 4 * s, width / 2, height - 30 * s, s };
    drawHitFeedback(feedback, draw, state.combo, state.errorsMs, layout);
    ImVec2 area = hitDistributionArea(layout), size = hitDistributionSize(s);
    game.distribution = { area.x, area.y, size.x, size.y };
}

static void beginCheck(){
    if (!noteInputActive()) return;
    char stamp[32];
    std::time_t clock = std::time(nullptr);
    std::strftime(stamp, sizeof stamp, "%Y-%m-%d-%H%M%S", std::localtime(&clock));
    checkBase = userDataDir() + "/check-song-" + stamp;
    startInputRecording();
    checkLog.clear();
    lastSync = -1.0;
    firstSyncRecording = -1.0;
    // The screen too, if FFmpeg is there
    const std::string ffmpeg = findFfmpeg(userDataDir() + "/addons");
    std::string error;
    if (!ffmpeg.empty() && !startScreenRecording(ffmpeg, checkBase + "-pictures.mp4", error)) TraceLog(LOG_WARNING, "Recording the screen: %s", error.c_str());
}

void waitForChecks(){
    if (checkVideo.joinable()) checkVideo.join();
}

static void saveCheck(){
    if (!inputRecording()) return;
    const std::string base = checkBase;
    std::string error;
    std::string screenError;
    const bool filmed = screenRecording() && stopScreenRecording(screenError);
    if (!screenError.empty()) TraceLog(LOG_WARNING, "Recording the screen: %s", screenError.c_str());
    if (!saveInputRecording(base, error)){
        checkSaved = "Check not saved: " + error;
        checkSavedAt = GetTime();
        return;
    }
    std::ofstream out(base + ".txt", std::ios::app);
    const GameplayOptions& options = game.options;
    out << "\n# the song: " << game.chart.title << ", part " << options.part << (game.options.practice.on ? ", practising" : "")
        << "; input offset " << options.inputOffsetSeconds * 1000.0f << " ms, offset " << options.offsetSeconds * 1000.0f << " ms\n";
    out << "# on the song's clock: what was heard and judged; sync <seconds into the recording> <song time> now and then\n";
    for (const std::string& line : checkLog) out << line << "\n";
    out << "\n# the notes as they stand at the end (a practice's: its last pass)\n#     time  note   how\n";
    for (const PlayNote& note : game.notes){
        out << TextFormat("%9.3f  %-5s  %s", note.time, pitchText(note.pitch).c_str(),
                          !note.judged ? "not reached" : !note.hit ? "missed" : note.wasPerfect ? "perfect" : "good");
        if (note.hit) out << TextFormat(" %+.0f ms", note.error * 1000.0f);
        out << "\n";
    }
    checkLog.clear();
    checkSaved = "Check saved: " + base + ".wav and .txt";
    checkSavedAt = GetTime();
    if (!filmed) return;

    // The sound under the pictures: the instrument from the start, and the song's audio where it played, from the two
    // clocks side by side (at the song's own speed only: a practice's slowed song isn't the file's)
    std::vector<VideoSound> sounds = { { base + ".wav", 0.0, 1.0f } };
    if (firstSyncRecording >= 0.0 && songSpeed() == 1.0f && !options.practice.noteByNote && !checkSongAudio.empty()){
        const double audioAtStart = firstSyncSong - firstSyncRecording + options.offsetSeconds; // into the audio file, when the recording began
        sounds.push_back({ checkSongAudio, -audioAtStart, 0.7f });
    }
    waitForChecks();
    checkVideoBusy = true;
    checkSaved += ", and the video as .mp4 in a moment";
    checkVideo = std::thread([ffmpeg = findFfmpeg(userDataDir() + "/addons"), base, sounds]{
        std::string error;
        std::error_code ignored;
        if (addSoundToVideo(ffmpeg, base + "-pictures.mp4", sounds, base + ".mp4", error)) std::filesystem::remove(base + "-pictures.mp4", ignored);
        else TraceLog(LOG_WARNING, "The check's video: %s", error.c_str());
        checkVideoBusy = false;
    });
}

void stopGameplay(){
    saveCheck();
    closeSongVideo();
    keepSongStretched(false);
    setSongSpeed(1.0f); // practice over: songs play at their own speed again
    inputScale = 1.0f;
    frozenTime = false;
    stopNoteInput();
    stopMidiInput();
    stopPianoKeys();
    unloadSong();
    setSongVolume(1.0f); // a trimmed song's fade-out isn't the next song's
    game.active = false;
}

GameResult gameplayResult(){
    GameResult result;
    const GameState& state = game.state;
    result.title = game.chart.title;
    result.partName = game.partName + (game.options.rhythmMode ? " (rhythm)" : "");
    result.score = state.score;
    result.maxCombo = state.maxCombo;
    result.perfectCount = state.perfectCount;
    result.nearCount = state.nearCount;
    result.missCount = state.missCount;
    result.totalNotes = (int)game.notes.size();
    result.accuracy = runAccuracy(state.perfectCount, state.nearCount, state.missCount);
    result.timing = timingStats(state.errorsMs);
    result.withInstrument = game.keys ? midiInputActive() : game.options.playWithInstrument; // the computer keyboard isn't an instrument
    result.fingerprint = game.fingerprint;
    result.errorsMs = state.errorsMs;
    result.distributionFrom = game.distribution;
    for (const PlayNote& note : game.notes){
        result.written.push_back({ note.time, std::max(note.length, note.writtenLength), note.pitch, note.stringIndex, note.hit, note.hit && note.wasPerfect,
                                  note.error * 1000.0f });
    }
    result.heard = heardLog;
    if (!game.keys) result.tuning = game.chart.frettedTracks[0].tuning;
    return result;
}

bool gameplayPractising(){
    return game.active && game.options.practice.on;
}

PracticeProgress practiceProgress(){
    return game.progress;
}

float gameplaySongTime(){
    return game.songTime;
}
