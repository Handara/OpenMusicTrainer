#include "learn/intervalexercise.h"

#include "audio/audio.h"
#include "core/music.h"
#include "input/noteinput.h"
#include "imgui.h"
#include "raylib.h"
#include "ui/ui.h"
#include "ui/theme.h"

#include <algorithm>
#include <random>

const double AUTO_NEXT_AFTER_S = 1.0;    // after a right answer, the next question plays by itself
const float ANSWER_BUTTON_WIDTH = 170.0f;
const float ANSWER_BUTTON_HEIGHT = 84.0f;
const int ANSWER_COLUMNS = 4;


const float LOWEST_EXPECTED_NOTE_HZ = 40.0f; // a bass's low E: anything from bass to voice can answer
const double QUESTION_TAIL_S = 0.35;         // after the question's last note starts, keep ignoring input this long
const double ANSWER_TIMEOUT_S = 5.0;         // a first note with no second one this long after is forgotten

IntervalExercise::IntervalExercise(const std::string& title, const IntervalConfig& config, const std::string& progressPath,
                                   const std::string& inputDevice)
    : title(title), progressPath(progressPath), inputDevice(inputDevice){
    trainer.config = config;
    trainer.progress = loadIntervalProgress(progressPath);
    trainer.rng.seed(std::random_device{}()); // different questions every session
    // Space and the number keys answer here, so ImGui's keyboard navigation (which also uses them) is off
    ImGui::GetIO().ConfigFlags &= ~ImGuiConfigFlags_NavEnableKeyboard;
    nextQuestion();
}

// The destructor puts back what the constructor changed, so leaving the exercise in any way restores it
IntervalExercise::~IntervalExercise(){
    stopPreviews();
    stopNoteInput();
    ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
}

void IntervalExercise::playInterval(int firstPitch, int secondPitch){
    stopPreviews(); // a replay shouldn't pile up on the last one
    playPreview(midiToFrequency((float)firstPitch));
    float gap = trainer.config.direction == IntervalDirection::Harmonic ? 0.0f : trainer.config.gapSeconds;
    playPreview(midiToFrequency((float)secondPitch), gap);
    // Through speakers the microphone hears these notes too: they mustn't count as the player's answer
    listenFrom = GetTime() + gap + QUESTION_TAIL_S;
    playedPitches.clear();
    playedText.clear();
}

void IntervalExercise::setAnswerByPlaying(bool on){
    inputError.clear();
    if (on && !startNoteInput(inputDevice, LOWEST_EXPECTED_NOTE_HZ, inputError)) on = false;
    if (!on) stopNoteInput();
    byPlaying = on;
    playedPitches.clear();
    playedText.clear();
}

// The answer is the distance between the two notes the player plays, whatever note they start on:
// hearing an interval and finding it on an instrument is the skill being practiced
void IntervalExercise::listenForPlayedAnswer(){
    for (const PlayedNote& note : updateNoteInput()){
        double startedAt = GetTime() - note.age;
        if (answered || startedAt < listenFrom) continue;
        if (playedPitches.empty()) firstPlayedAt = startedAt;
        playedPitches.push_back(note.pitch);
        playedText += (playedText.empty() ? "" : "  ->  ") + std::string(pitchClassName(note.pitch)) + std::to_string(pitchOctave(note.pitch));
        if (playedPitches.size() < 2) continue;

        int distance = std::abs(playedPitches[1] - playedPitches[0]);
        while (distance > INTERVAL_COUNT) distance -= INTERVAL_COUNT; // an interval plus octaves counts as the interval
        if (distance == 0){
            playedText += "   (the same note twice: try again)";
            playedPitches.clear();
            continue;
        }
        submit(distance);
    }
    if (playedPitches.size() == 1 && GetTime() - firstPlayedAt > ANSWER_TIMEOUT_S){
        playedPitches.clear();
        playedText.clear();
    }
}

void IntervalExercise::nextQuestion(){
    question = nextIntervalQuestion(trainer);
    answered = false;
    playInterval(question.firstPitch, question.secondPitch);
}

void IntervalExercise::submit(int semitones){
    if (answered) return;
    IntervalAnswerResult result = answerInterval(trainer, question, semitones);
    answered = true;
    lastCorrect = result.correct;
    lastAnswer = semitones;
    answeredAt = GetTime();
    newlyUnlocked = result.unlocked;
    sessionAsked++;
    if (result.correct) sessionCorrect++;

    // Saved after every answer: closing the game at any moment loses nothing
    saveError.clear();
    if (!saveIntervalProgress(progressPath, trainer.progress, saveError)) TraceLog(LOG_WARNING, "Progress: %s", saveError.c_str());
}

void IntervalExercise::update(){
    if (byPlaying) listenForPlayedAnswer();
    if (ImGui::IsKeyPressed(ImGuiKey_Space) || ImGui::IsKeyPressed(ImGuiKey_Enter)){
        if (answered) nextQuestion();
        else playInterval(question.firstPitch, question.secondPitch);
    }
    // Number keys pick answers in the order the buttons show them: 1 = first button ... 0 = tenth
    std::vector<int> choices = unlockedIntervals(trainer.config, trainer.progress);
    std::sort(choices.begin(), choices.end());
    for (int i = 0; i < (int)choices.size() && i < 10; i++){
        ImGuiKey key = i < 9 ? (ImGuiKey)(ImGuiKey_1 + i) : ImGuiKey_0;
        if (ImGui::IsKeyPressed(key)) submit(choices[i]);
    }
    // A right answer moves on by itself (unless something new was just unlocked: give time to read it)
    if (answered && lastCorrect && newlyUnlocked == 0 && GetTime() - answeredAt > AUTO_NEXT_AFTER_S) nextQuestion();
}

void IntervalExercise::draw(){
    const IntervalProgress& progress = trainer.progress;
    const IntervalConfig& config = trainer.config;
    menuTitle(title.c_str());

    int unlocked = (int)unlockedIntervals(config, progress).size();
    int poolSize = (int)config.pool.size();
    int recentCorrect = (int)std::count(progress.recent.begin(), progress.recent.end(), true);
    int percent = sessionAsked > 0 ? 100 * sessionCorrect / sessionAsked : 0;
    centeredColoredText(TextFormat("%d of %d intervals    This session: %d/%d (%d%%)    Streak %d, best %d",
                                   unlocked, poolSize, sessionCorrect, sessionAsked, percent,
                                   trainer.streak, progress.bestStreak), uiColor(UiColor::Dim));
    if (unlocked < poolSize){
        centeredColoredText(TextFormat("Next interval unlocks at %d right out of your last %d: now %d",
                                       config.unlockCorrect, config.unlockWindow, recentCorrect), uiColor(UiColor::Dim));
    }
    ImGui::Dummy(ImVec2(0, 10));
    if (menuButton(answered ? "Next  [Space]" : "Play again  [Space]")){
        if (answered) nextQuestion();
        else playInterval(question.firstPitch, question.secondPitch);
    }
    ImGui::Dummy(ImVec2(0, 10));

    // Answer buttons in size order (easier to find than unlock order), with each interval's accuracy
    std::vector<int> choices = unlockedIntervals(trainer.config, progress);
    std::sort(choices.begin(), choices.end());
    int columns = std::min(ANSWER_COLUMNS, (int)choices.size());
    float rowWidth = columns * ANSWER_BUTTON_WIDTH + (columns - 1) * ImGui::GetStyle().ItemSpacing.x;
    for (int i = 0; i < (int)choices.size(); i++){
        int semitones = choices[i];
        const IntervalInfo& info = intervalInfo(semitones);
        const IntervalStats& stats = progress.stats[semitones];
        std::string label = std::string(info.shortName) + "\n" + info.name;
        if (stats.asked > 0) label += TextFormat("\n%d%%", 100 * stats.correct / stats.asked);
        label += TextFormat("##%d", semitones); // ## hides the rest from display but keeps each button's id unique

        if (i % columns == 0) ImGui::SetCursorPosX((ImGui::GetWindowWidth() - rowWidth) / 2);
        else ImGui::SameLine();

        // After an answer: the right one lights green, a wrong pick red
        int colors = 0;
        UiColor fill = UiColor::Card;
        if (answered && semitones == question.semitones) fill = UiColor::Good;
        else if (answered && semitones == lastAnswer) fill = UiColor::Bad;
        if (fill != UiColor::Card){
            ImGui::PushStyleColor(ImGuiCol_Button, uiColorVec(fill));
            ImGui::PushStyleColor(ImGuiCol_Text, uiColorVec(UiColor::Card)); // light text on the filled button
            colors += 2;
        }
        if (ImGui::Button(label.c_str(), ImVec2(ANSWER_BUTTON_WIDTH, ANSWER_BUTTON_HEIGHT))) submit(semitones);
        ImGui::PopStyleColor(colors);
    }

    // Answering by playing: a checkbox, then what the microphone heard
    ImGui::Dummy(ImVec2(0, 6));
    bool playing = byPlaying;
    ImGui::SetCursorPosX((ImGui::GetWindowWidth() - 360) / 2);
    if (ImGui::Checkbox("Answer by playing (or singing)", &playing)) setAnswerByPlaying(playing);
    if (!inputError.empty()) centeredErrorText("No input device: " + inputError);
    if (byPlaying){
        centeredColoredText(playedText.empty() ? "Play the two notes you heard, starting on any note"
                                               : TextFormat("You played:  %s", playedText.c_str()), uiColor(UiColor::Dim));
        centeredColoredText(TextFormat("Input level %.0f dB", noteInputLevelDb()), uiColor(UiColor::Dim));
    }

    ImGui::Dummy(ImVec2(0, 10));
    if (answered){
        const IntervalInfo& right = intervalInfo(question.semitones);
        if (lastCorrect){
            centeredColoredText(TextFormat("Right! %s", right.name), uiColor(UiColor::Good));
        } else {
            centeredColoredText(TextFormat("It was a %s. You answered %s.", right.name, intervalInfo(lastAnswer).name), uiColor(UiColor::Bad));
            // Hearing both side by side is how the difference gets learned
            float buttonsWidth = 2 * 240.0f + ImGui::GetStyle().ItemSpacing.x;
            ImGui::SetCursorPosX((ImGui::GetWindowWidth() - buttonsWidth) / 2);
            if (ImGui::Button(TextFormat("Hear the %s", right.shortName), ImVec2(240, 44))) playInterval(question.firstPitch, question.secondPitch);
            ImGui::SameLine();
            int yourSecond = question.firstPitch + (question.secondPitch > question.firstPitch ? lastAnswer : -lastAnswer);
            if (trainer.config.direction == IntervalDirection::Harmonic) yourSecond = question.firstPitch + lastAnswer;
            if (ImGui::Button(TextFormat("Hear your %s", intervalInfo(lastAnswer).shortName), ImVec2(240, 44))) playInterval(question.firstPitch, yourSecond);
        }
        if (newlyUnlocked != 0){
            ImGui::Dummy(ImVec2(0, 6));
            centeredColoredText(TextFormat("New interval unlocked: %s!", intervalInfo(newlyUnlocked).name), uiColor(UiColor::Good));
            ImGui::SetCursorPosX((ImGui::GetWindowWidth() - 240) / 2);
            if (ImGui::Button(TextFormat("Hear the %s", intervalInfo(newlyUnlocked).shortName), ImVec2(240, 44))){
                playInterval(question.firstPitch, question.firstPitch + newlyUnlocked);
            }
        }
    }
    if (!saveError.empty()) centeredErrorText("Progress could not be saved: " + saveError);

    ImGui::Dummy(ImVec2(0, 16));
    if (menuButton("Back")) leave = true;
}
