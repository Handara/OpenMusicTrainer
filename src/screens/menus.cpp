#include "screens/menus.h"

#include "raylib.h"
#include "screens/tuner.h"
#include "ui/ui.h"

// Immediate mode: these functions run every frame, drawing the widgets and reacting to clicks in the same call.

MainMenuChoice mainMenuScreen(const std::string& error){
    MainMenuChoice choice = MainMenuChoice::None;
    beginMenu("MainMenu");
    menuTitle("OpenMusicTrainer");
    focusNextWhenMenuAppears(); // keyboard navigation starts on Play
    if (menuButton("Play")) choice = MainMenuChoice::Play;
    if (menuButton("Learn")) choice = MainMenuChoice::Learn;
    if (menuButton("Song editor")) choice = MainMenuChoice::Editor;
    if (menuButton("Lesson editor")) choice = MainMenuChoice::LessonEditor;
    if (menuButton("Tuner")) choice = MainMenuChoice::Tuner;
    if (menuButton("Settings")) choice = MainMenuChoice::Settings;

    if (menuButton("Quit")) choice = MainMenuChoice::Quit;
    if (!error.empty()){
        ImGui::Dummy(ImVec2(0, 10));
        centeredErrorText(error);
    }
    ImGui::End();
    return choice;
}

SongSelectChoice songSelectScreen(const char* title, const std::vector<SongEntry>& songs, const std::string& error,
                                  bool forEditing){
    SongSelectChoice choice;
    beginMenu(title);
    menuTitle(title);

    if (songs.empty()) centeredText("No songs found");
    for (int i = 0; i < (int)songs.size(); i++){
        const SongEntry& song = songs[i];
        std::string label = song.artist.empty() ? song.title : song.title + "  -  " + song.artist;
        if (forEditing && song.builtIn) label += "  (built-in)";

        ImGui::PushID(i); // two songs with the same title must still be different widgets
        if (i == 0) focusNextWhenMenuAppears();
        if (!song.error.empty()){
            ImGui::BeginDisabled();
            menuButton(label.c_str());
            ImGui::EndDisabled();
            centeredErrorText(song.error);
        } else if (menuButton(label.c_str())){
            choice.songIndex = i;
        }
        ImGui::PopID();
    }

    if (!error.empty()){
        ImGui::Dummy(ImVec2(0, 10));
        centeredErrorText(error);
    }
    ImGui::Dummy(ImVec2(0, 20));
    if (menuButton("Open data folder")) choice.openDataFolder = true;
    if (menuButton("Back")) choice.back = true;
    ImGui::End();
    return choice;
}

ResultsChoice resultsScreen(const GameResult& result){
    ResultsChoice choice = ResultsChoice::None;
    int hits = result.perfectCount + result.nearCount;
    float accuracy = result.totalNotes > 0 ? 100.0f * hits / result.totalNotes : 0.0f;

    beginMenu("Results");
    menuTitle(result.title.c_str());
    centeredText(TextFormat("Score  %08d", result.score));
    centeredText(TextFormat("Max combo  %d", result.maxCombo));
    centeredText(TextFormat("Perfect %d    Near %d    Miss %d", result.perfectCount, result.nearCount, result.missCount));
    centeredText(TextFormat("Notes hit  %d / %d  (%.1f%%)", hits, result.totalNotes, accuracy));
    ImGui::Dummy(ImVec2(0, 30));

    focusNextWhenMenuAppears();
    if (menuButton("Retry")) choice = ResultsChoice::Retry;
    if (menuButton("Back to songs")) choice = ResultsChoice::BackToSongs;
    ImGui::End();
    return choice;
}

bool tunerScreen(){
    beginMenu("Tuner");
    drawTuner();
    ImGui::Dummy(ImVec2(0, 20));
    focusNextWhenMenuAppears();
    bool back = menuButton("Back");
    ImGui::End();
    return back;
}
