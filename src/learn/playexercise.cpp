#include "learn/playexercise.h"

#include "imgui.h"
#include "raylib.h"
#include "ui/ui.h"
#include "ui/theme.h"

#include <algorithm>


PlayExercise::PlayExercise(const std::string& chartPath, const GameplayOptions& options) : chartPath(chartPath), options(options){
    start();
}

PlayExercise::~PlayExercise(){
    stopGameplay(); // safe to call more than once
}

void PlayExercise::start(){
    error.clear();
    playing = startGameplay(chartPath, options, error);
}

static int percentHit(const GameResult& result){
    return result.totalNotes > 0 ? 100 * (result.perfectCount + result.nearCount) / result.totalNotes : 0;
}

bool PlayExercise::takeFinishedRun(int& percent){
    if (finishedPercent < 0) return false;
    percent = finishedPercent;
    finishedPercent = -1;
    return true;
}

void PlayExercise::update(){
    if (!playing) return;
    if (updateGameplay()) return;
    // The song is over: keep the result, and the best run for the lesson's goal
    last = gameplayResult();
    hasResult = true;
    bestPercent = std::max(bestPercent, percentHit(last));
    finishedPercent = percentHit(last);
    stopGameplay();
    playing = false;
}

void PlayExercise::draw(){
    if (playing){
        drawGameplay(); // raylib drawing, under the UI (the lesson's goal bar)
        return;
    }
    menuTitle(hasResult ? last.title.c_str() : "Play along");
    if (!error.empty()) centeredErrorText(error);
    if (hasResult){
        centeredText(TextFormat("Hit %d of %d notes (%d%%)", last.perfectCount + last.nearCount, last.totalNotes, percentHit(last)));
        centeredColoredText(TextFormat("Perfect %d    Near %d    Miss %d    Best run %d%%", last.perfectCount, last.nearCount,
                                       last.missCount, bestPercent), uiColor(UiColor::Dim));
    }
    ImGui::Dummy(ImVec2(0, 20));
    if (error.empty() && menuButton("Play again")) start();
    if (menuButton("Back")) leave = true;
}
