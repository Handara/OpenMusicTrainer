#include "screens/menus.h"

#include "raylib.h"
#include "screens/tuner.h"
#include "ui/menulist.h"
#include "ui/theme.h"
#include "ui/ui.h"

// Immediate mode: these functions run every frame, drawing the widgets and reacting to clicks in the same call.

// The list screens' area: under the title, down to the hints, and the width of the left part of the screen
static MenuListArea listArea(float widthShare){
    float width = ImGui::GetWindowWidth(), height = ImGui::GetWindowHeight(), s = menuScale();
    return { ImVec2(width * 0.07f, height * 0.25f), width * widthShare, height * 0.66f - 20 * s, s };
}

SongSelectChoice songSelectScreen(const char* title, const std::vector<SongEntry>& songs, const std::string& error,
                                  bool forEditing){
    static MenuList playList, editList; // each list keeps its selection
    MenuList& list = forEditing ? editList : playList;
    SongSelectChoice choice;
    beginMenu(title);
    float s = menuScale();
    menuScreenTitle(title, s);

    // The songs, then the data folder and Back
    std::vector<MenuRow> rows;
    for (const SongEntry& song : songs){
        MenuRow row;
        row.label = song.title;
        row.detail = song.artist;
        if (forEditing && song.builtIn) row.detail += song.artist.empty() ? "built-in" : "  ·  built-in";
        row.note = song.error;
        row.disabled = !song.error.empty();
        rows.push_back(row);
    }
    if (songs.empty()){
        MenuRow none;
        none.label = "No songs yet";
        none.detail = "put song folders in the data folder";
        none.disabled = true;
        rows.push_back(none);
    }
    const int openFolder = (int)rows.size(), back = openFolder + 1;
    rows.push_back(actionRow("Open data folder"));
    rows.push_back(actionRow("Back", "Esc"));

    int confirmed = menuList(list, rows, listArea(0.8f));
    if (confirmed >= 0 && confirmed < (int)songs.size()) choice.songIndex = confirmed;
    if (confirmed == openFolder) choice.openDataFolder = true;
    if (confirmed == back) choice.back = true;

    if (!error.empty()){
        ImGui::SetCursorPos(ImVec2(ImGui::GetWindowWidth() * 0.07f, ImGui::GetWindowHeight() * 0.17f + 20 * s));
        ImGui::TextColored(uiColorVec(UiColor::Bad), "%s", error.c_str());
    }
    menuScreenHint(forEditing ? "Up/Down  choose    Enter  edit    Esc  back" : "Up/Down  choose    Enter  play    Esc  back", s);
    ImGui::End();
    return choice;
}

ResultsChoice resultsScreen(const GameResult& result){
    static MenuList list;
    static const std::vector<MenuRow> rows = { actionRow("Retry"), actionRow("Back to songs", "Esc") };
    ResultsChoice choice = ResultsChoice::None;
    int hits = result.perfectCount + result.nearCount;
    float accuracy = result.totalNotes > 0 ? 100.0f * hits / result.totalNotes : 0.0f;

    beginMenu("Results");
    float s = menuScale(), width = ImGui::GetWindowWidth(), height = ImGui::GetWindowHeight();
    menuScreenTitle(result.title.c_str(), s);
    if (ImGui::IsWindowAppearing()) list.selected = 0; // Retry first, every time

    // The numbers, on a card like the main menu's
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const UiFonts& fonts = uiFonts();
    ImVec2 card(width * 0.58f, height * 0.25f);
    float cardWidth = width * 0.35f, pad = 26 * s;
    float cardHeight = pad * 2 + 3 * (13 * s + 8 * s) + 2 * 46 * s + 26 * s + 2 * 18 * s;
    draw->AddRectFilled(ImVec2(card.x, card.y + 3 * s), ImVec2(card.x + cardWidth, card.y + cardHeight + 3 * s), uiColor(UiColor::Ink, 0.04f), 10 * s);
    draw->AddRectFilled(card, ImVec2(card.x + cardWidth, card.y + cardHeight), uiColor(UiColor::Card), 10 * s);
    float x = card.x + pad, y = card.y + pad;
    auto label = [&](const char* text){
        draw->AddText(fonts.mono, 13 * s, ImVec2(x, y), uiColor(UiColor::Dim), text);
        y += 13 * s + 8 * s;
    };
    label("ACCURACY");
    draw->AddText(fonts.heavy, 46 * s, ImVec2(x, y), uiColor(UiColor::Ink), TextFormat("%.1f%%", accuracy));
    y += 46 * s + 13 * s;
    label("SCORE");
    draw->AddText(fonts.heavy, 46 * s, ImVec2(x, y), uiColor(UiColor::Ink), TextFormat("%d", result.score));
    y += 46 * s + 13 * s;
    label("NOTES");
    draw->AddText(fonts.text, 18 * s, ImVec2(x, y), uiColor(UiColor::Ink),
                  TextFormat("%d of %d hit  ·  best combo %d", hits, result.totalNotes, result.maxCombo));
    y += 18 * s + 6 * s;
    draw->AddText(fonts.text, 18 * s, ImVec2(x, y), uiColor(UiColor::Dim),
                  TextFormat("Perfect %d  ·  Near %d  ·  Miss %d", result.perfectCount, result.nearCount, result.missCount));

    int confirmed = menuList(list, rows, listArea(0.45f));
    if (confirmed == 0) choice = ResultsChoice::Retry;
    if (confirmed == 1) choice = ResultsChoice::BackToSongs;
    menuScreenHint("Enter  choose    Esc  back to songs", s);
    ImGui::End();
    return choice;
}

bool tunerScreen(){
    beginMenu("Tuner");
    menuTitle("Tuner");
    drawTuner();
    ImGui::Dummy(ImVec2(0, 20));
    focusNextWhenMenuAppears();
    bool back = menuButton("Back");
    ImGui::End();
    return back;
}
