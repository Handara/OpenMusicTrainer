#include "screens/gameplay.h"

#include "audio/audio.h"
#include "core/chart.h"
#include "core/judge.h"
#include "core/music.h"
#include "core/score.h"
#include "imgui.h"
#include "input/midi.h"
#include "input/noteinput.h"
#include "raylib.h"
#include "ui/hitfeedback.h"
#include "ui/menulist.h"
#include "ui/theme.h"
#include "views/noteviews.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
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

const int MAX_LANES = 6; // limited by the number keys and the vertical layout for now
const int HIT_LINE_X = 180;
const float RHYTHM_FILL_PER_PERFECT = 0.12f;

const int laneKeys[MAX_LANES] = { KEY_ONE, KEY_TWO, KEY_THREE, KEY_FOUR, KEY_FIVE, KEY_SIX };

const int MAX_MULTIPLIER = 4;
const double LEAD_IN_S = 2.0; // starting part-way into a song, it plays this long before the first note
const double RESUME_RUNUP_S = 1.5; // resuming, the song picks up this long before where it was paused

static HitFeedback feedback; // the judgements, timing bar and combo shown over the play screen

static void scoreMisses(GameState& state, int count){
    if (count == 0) return;
    feedbackMiss(feedback, state.combo);
    state.combo = 0;
    state.multiplier = 1;
    state.missCount += count;
    state.rhythm = 0.0f;
}

// Scoring is the game's own rule on top of judging: perfect hits build the multiplier, near hits don't
static void scoreHit(GameState& state, Judgement judgement, int notesHit, double error){
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
    feedbackHit(feedback, judgement, error, state.combo);
}

// Number keys 1 to 6 stand for the strings, lowest first
static void handleKeyboard(std::vector<PlayNote>& notes, GameState& state, float songTime, int laneCount, bool hitSounds){
    for (int lane = 0; lane < laneCount; lane++){
        if (!IsKeyPressed(laneKeys[lane])) continue;
        PlayerInput press;
        press.time = songTime;
        press.stringIndex = lane;
        JudgeResult result = judgeInput(notes, press);
        if (result.judgement == Judgement::Ignored) continue;
        scoreHit(state, result.judgement, result.notesHit, result.error);
        if (hitSounds) playPreview(midiToFrequency((float)result.pitch)); // the key plays the note it hit
    }
}

// With an instrument: each played note is placed in song time (now, minus how long ago it started, minus the
// input device's delay) and judged by its pitch
static void handleInstrument(std::vector<PlayNote>& notes, GameState& state, float songTime, float inputOffset,
                             int& lastPlayedPitch){
    for (const PlayedNote& played : updateNoteInput()){
        PlayerInput input;
        input.time = songTime - played.age - inputOffset;
        input.pitch = played.pitch;
        JudgeResult result = judgeInput(notes, input);
        if (result.judgement != Judgement::Ignored) scoreHit(state, result.judgement, result.notesHit, result.error);
        lastPlayedPitch = played.pitch;
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
    int lastPlayedPitch = -1; // the latest note heard from the instrument, shown so the player can trust the input
    float hitLineX = 180.0f;  // where the views put the hit line, for the judgements drawn at it
    bool paused = false;
    bool keys = false;            // playing a keys part, on a MIDI keyboard
    Rectangle distribution = {}; // where the HUD's timing distribution is: the results screen grows it from there
    float resumeAt = -1.0f;   // after a resume: the song time it was paused at (GET READY shows until then)
    std::string fingerprint;  // of the part being played
    bool active = false;
} game;

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
    if (options.part < 0 || options.part >= partCount(chart)){
        error = "the chart has no part " + std::to_string(options.part + 1) + " to play";
        return false;
    }
    // Only the part being played is kept: everything below works on the chart's first track. A keys part becomes a
    // one-string instrument whose frets are its pitches: judging, scoring and the sheet music then work as for
    // any part.
    game.fingerprint = partFingerprint(chart, options.part); // of the whole part, before anything is left out
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
    // Starting part-way: the notes before are left out, so the score (the sheet music) is built without them too
    std::vector<FrettedNote>& chartNotes = game.chart.frettedTracks[0].notes;
    chartNotes.erase(chartNotes.begin(), std::lower_bound(chartNotes.begin(), chartNotes.end(), fromTick,
                     [](const FrettedNote& note, int tick){ return note.tick < tick; }));
    const FrettedTrack& track = game.chart.frettedTracks[0];
    if (!game.keys && (int)track.tuning.size() > MAX_LANES){
        error = "track '" + track.name + "' has " + std::to_string(track.tuning.size())
                + " strings, the prototype supports up to " + std::to_string(MAX_LANES);
        return false;
    }

    if (!loadSong(audioPath, error)) return false;

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
    game.state = {};
    feedback = {};
    game.paused = false;
    game.resumeAt = -1.0f;
    game.state.multiplier = 1;
    game.options = options;
    game.lastPlayedPitch = -1;
    // A keys part is played on a MIDI keyboard, and only there: the part waits for one to be connected
    if (game.keys && !startMidiInput(options.midiDevice, error)){
        error = "This part is played on a MIDI keyboard: " + error + " (Settings > Audio)";
        unloadSong();
        return false;
    }
    if (options.playWithInstrument && !game.keys){
        // Listen down to just below the track's lowest string: a bass or a drop tuning gets its own range
        float lowest = midiToFrequency((float)*std::min_element(track.tuning.begin(), track.tuning.end())) * 0.9f;
        if (!startNoteInput(options.inputDevice, lowest, error)){
            error = "Playing with your instrument: " + error + " (Settings > Gameplay switches to the keyboard)";
            unloadSong();
            return false;
        }
    }
    game.songTime = 0.0f;
    game.active = true;
    if (fromTick > 0) playSongFrom(tickToSeconds(game.chart, fromTick) - LEAD_IN_S);
    else playSong(false);
    return true;
}

void pauseGameplay(){
    if (!game.active || game.paused) return;
    stopSong();
    stopPreviews();
    game.paused = true;
}

void resumeGameplay(){
    if (!game.active || !game.paused) return;
    game.paused = false;
    game.resumeAt = game.songTime;
    playSongFrom(game.songTime + game.options.offsetSeconds - RESUME_RUNUP_S);
}

bool gameplayPaused(){
    return game.active && game.paused;
}

bool updateGameplay(){
    if (!game.active) return false;
    // Away from the game (another window has the focus), it waits
    if (!IsWindowFocused()) pauseGameplay();
    if (game.paused){
        if (noteInputActive()) updateNoteInput(); // what's played while paused is thrown away, not judged later
        if (midiInputActive()) updateMidiInput();
        return true;
    }

    // The song's playback position is the clock: notes stay in sync with the music even if frames stutter
    // The offset shifts the whole game against the audio: if sound reaches your ears late (Bluetooth,
    // slow drivers), a positive offset moves notes and judging later to match what you hear
    game.songTime = (float)(songPosition() - game.options.offsetSeconds);

    for (PlayNote& note : game.notes){
        if (note.hitFlash > 0.0f) note.hitFlash -= GetFrameTime();
    }
    const FrettedTrack& track = game.chart.frettedTracks[0];
    handleKeyboard(game.notes, game.state, game.songTime, (int)track.tuning.size(), game.options.hitSounds);
    if (noteInputActive()) handleInstrument(game.notes, game.state, game.songTime, game.options.inputOffsetSeconds, game.lastPlayedPitch);
    if (midiInputActive()){
        // Each key on its own: pressing one note of a chord doesn't play the rest
        for (const PlayedNote& played : updateMidiInput()){
            PlayerInput input;
            input.time = game.songTime - played.age;
            input.pitch = played.pitch;
            input.completesChord = false;
            JudgeResult result = judgeInput(game.notes, input);
            if (result.judgement != Judgement::Ignored) scoreHit(game.state, result.judgement, result.notesHit, result.error);
            game.lastPlayedPitch = played.pitch;
        }
    }
    scoreMisses(game.state, markMisses(game.notes, game.songTime));

    if (songEnded()){
        // Anything still unjudged when the music stops counts as missed
        scoreMisses(game.state, markMisses(game.notes, game.songTime + 1e9));
        return false;
    }
    return true;
}

void drawGameplay(){
    if (!game.active) return;
    ClearBackground(themeColor(UiColor::Background));
    TimeAxis axis = { game.songTime, (float)HIT_LINE_X, game.options.noteSpeed };
    float width = (float)GetScreenWidth(), height = (float)GetScreenHeight();
    Rectangle viewsArea = { 0, height * 0.14f, width, height * 0.72f }; // below the HUD, above the combo and timing bar
    if (game.keys){
        game.hitLineX = drawKeysViews(viewsArea, game.options.noteViews, game.notes, game.score, axis, midiInputActive() ? midiKeysDown() : nullptr);
        return;
    }
    game.hitLineX = drawNoteViews(viewsArea, game.options.noteViews, game.notes, game.score, game.chart.frettedTracks[0].tuning,
                  game.options.lowStringOnTop, axis);
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

    // The rhythm meter: a thin brass line along the top edge, filling as perfect hits keep coming
    draw->AddRectFilled(ImVec2(0, 0), ImVec2(width, 4 * s), uiColor(UiColor::StaffLine));
    draw->AddRectFilled(ImVec2(0, 0), ImVec2(width * state.rhythm, 4 * s), uiColor(UiColor::Accent));

    // The song on the left, with what the instrument is heard playing, so the player can trust the input
    draw->AddText(fonts.bold, 24 * s, ImVec2(margin, top), uiColor(UiColor::Ink), game.chart.title.c_str());
    std::string below = game.chart.artist;
    if (game.options.playWithInstrument){
        std::string heard = game.lastPlayedPitch >= 0
            ? TextFormat("You played %s%d", pitchClassName(game.lastPlayedPitch), pitchOctave(game.lastPlayedPitch)) : "Listening...";
        below += below.empty() ? heard : "  ·  " + heard;
    }
    draw->AddText(fonts.text, 16 * s, ImVec2(margin, top + 30 * s), uiColor(UiColor::Dim), below.c_str());

    // The score on the right, the combo and multiplier under it: the multiplier in brass once it's working
    const char* score = TextFormat("%d", state.score);
    draw->AddText(fonts.heavy, 34 * s, ImVec2(width - margin - textWidth(fonts.heavy, 34 * s, score), top - 4 * s), uiColor(UiColor::Ink), score);
    const char* multiplier = TextFormat("x%d", state.multiplier);
    float multiplierWidth = textWidth(fonts.mono, 15 * s, multiplier);
    draw->AddText(fonts.mono, 15 * s, ImVec2(width - margin - multiplierWidth, top + 38 * s),
                  uiColor(state.multiplier > 1 ? UiColor::Accent : UiColor::Dim), multiplier);

    // The judgements at the hit line, just above the notes; the combo and the timing bar under them, centered
    const float height = ImGui::GetIO().DisplaySize.y;
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

void stopGameplay(){
    stopNoteInput();
    stopMidiInput();
    unloadSong();
    game.active = false;
}

GameResult gameplayResult(){
    GameResult result;
    const GameState& state = game.state;
    result.title = game.chart.title;
    result.partName = game.chart.frettedTracks.empty() ? "" : game.chart.frettedTracks[0].name;
    result.score = state.score;
    result.maxCombo = state.maxCombo;
    result.perfectCount = state.perfectCount;
    result.nearCount = state.nearCount;
    result.missCount = state.missCount;
    result.totalNotes = (int)game.notes.size();
    result.accuracy = runAccuracy(state.perfectCount, state.nearCount, state.missCount);
    result.timing = timingStats(state.errorsMs);
    result.withInstrument = game.options.playWithInstrument;
    result.fingerprint = game.fingerprint;
    result.errorsMs = state.errorsMs;
    result.distributionFrom = game.distribution;
    return result;
}
