#include "screens/menus.h"

#include "raylib.h"
#include "screens/tuner.h"
#include "ui/menulist.h"
#include "ui/theme.h"
#include "ui/ui.h"

#include <cmath>

// Immediate mode: these functions run every frame, drawing the widgets and reacting to clicks in the same call.

// The list screens' area: under the title, down to the hints, and the width of the left part of the screen
static MenuListArea listArea(float widthShare){
    float width = ImGui::GetWindowWidth(), height = ImGui::GetWindowHeight(), s = menuScale();
    return { ImVec2(width * 0.07f, height * 0.25f), width * widthShare, height * 0.66f - 20 * s, s };
}

static UiColor gradeColor(Grade grade){
    switch (grade){
        case Grade::SS: case Grade::S: return UiColor::Accent; // brass: the best there is
        case Grade::A:                 return UiColor::Good;
        case Grade::D:                 return UiColor::Bad;
        default:                       return UiColor::Ink;
    }
}

// The selected song on a card beside the list: its parts, and the best run on each
static void drawSongCard(const SongEntry& song, float s){
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const UiFonts& fonts = uiFonts();
    float width = ImGui::GetWindowWidth(), height = ImGui::GetWindowHeight();
    ImVec2 card(width * 0.55f, height * 0.25f);
    float cardWidth = width * 0.38f, pad = 26 * s, inner = cardWidth - 2 * pad;
    float partHeight = 64 * s;
    float cardHeight = pad * 2 + 30 * s + (song.artist.empty() ? 0 : 24 * s) + 16 * s + (float)song.parts.size() * partHeight;
    if (song.parts.empty()) return;
    draw->AddRectFilled(ImVec2(card.x, card.y + 3 * s), ImVec2(card.x + cardWidth, card.y + cardHeight + 3 * s), uiColor(UiColor::Ink, 0.04f), 10 * s);
    draw->AddRectFilled(card, ImVec2(card.x + cardWidth, card.y + cardHeight), uiColor(UiColor::Card), 10 * s);
    float x = card.x + pad, y = card.y + pad;
    draw->AddText(fonts.bold, 26 * s, ImVec2(x, y), uiColor(UiColor::Ink), song.title.c_str(), nullptr, inner);
    y += 30 * s;
    if (!song.artist.empty()){
        draw->AddText(fonts.text, 18 * s, ImVec2(x, y), uiColor(UiColor::Dim), song.artist.c_str());
        y += 24 * s;
    }
    y += 16 * s;
    for (const SongPart& part : song.parts){
        const char* instrument = part.type == InstrumentType::Bass ? "BASS" : "GUITAR";
        draw->AddText(fonts.mono, 13 * s, ImVec2(x, y), uiColor(UiColor::Dim),
                      TextFormat("%s  ·  %s, %d STRINGS", part.name.c_str(), instrument, part.stringCount));
        float rowY = y + 20 * s;
        if (!part.played){
            draw->AddText(fonts.text, 18 * s, ImVec2(x, rowY + 4 * s), uiColor(UiColor::Dim), "Not played yet");
        } else {
            const RunRecord& best = part.best;
            draw->AddText(fonts.heavy, 30 * s, ImVec2(x, rowY - 2 * s), uiColor(gradeColor(best.grade())), gradeName(best.grade()));
            draw->AddText(fonts.bold, 20 * s, ImVec2(x + 56 * s, rowY + 4 * s), uiColor(UiColor::Ink),
                          TextFormat("%.2f%%   %d", best.accuracy, best.score));
            if (best.fullCombo()){
                draw->AddText(fonts.mono, 13 * s, ImVec2(x + inner - 30 * s, rowY + 9 * s), uiColor(UiColor::Accent), "FC");
            }
        }
        y += partHeight;
    }
}

static int choosingPartOf = -1; // the song whose parts are listed, -1 when the songs are

bool songSelectBack(){
    if (choosingPartOf < 0) return false;
    choosingPartOf = -1;
    return true;
}

static const char* instrumentName(InstrumentType type){
    return type == InstrumentType::Bass ? "bass" : "guitar";
}

// The song's parts, one level down from the songs: "Melody  guitar, 6 strings", "Bass  bass, 4 strings", Back
static void partList(const SongEntry& song, SongSelectChoice& choice){
    static MenuList list;
    float s = menuScale();
    menuScreenTitle(song.title.c_str(), s);
    if (ImGui::IsWindowAppearing()) list.selected = 0;
    std::vector<MenuRow> rows;
    for (const SongPart& part : song.parts){
        MenuRow row;
        row.label = part.name.empty() ? instrumentName(part.type) : part.name;
        row.detail = TextFormat("%s, %d strings", instrumentName(part.type), part.stringCount);
        rows.push_back(row);
    }
    const int back = (int)rows.size();
    rows.push_back(actionRow("Back", "Esc"));
    int confirmed = menuList(list, rows, listArea(0.45f));
    drawSongCard(song, s);
    if (confirmed >= 0 && confirmed < back){
        choice.songIndex = choosingPartOf;
        choice.part = confirmed;
        choosingPartOf = -1;
    }
    if (confirmed == back) choosingPartOf = -1;
    menuScreenHint("Up/Down  choose    Enter  play    Esc  back to songs", s);
}

SongSelectChoice songSelectScreen(const char* title, const std::vector<SongEntry>& songs, const std::string& error,
                                  const std::string& notice, bool forEditing){
    static MenuList playList, editList; // each list keeps its selection
    MenuList& list = forEditing ? editList : playList;
    SongSelectChoice choice;
    beginMenu(title);
    float s = menuScale();
    if (!forEditing && choosingPartOf >= 0 && choosingPartOf < (int)songs.size()){
        partList(songs[choosingPartOf], choice);
        ImGui::End();
        return choice;
    }
    choosingPartOf = -1;
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
    // Editing, a new song can be made too, from the player's own audio
    const int newSong = forEditing ? (int)rows.size() : -2;
    if (forEditing) rows.push_back(actionRow("New song from audio"));
    const int openFolder = (int)rows.size(), back = openFolder + 1;
    rows.push_back(actionRow("Open data folder"));
    rows.push_back(actionRow("Back", "Esc"));

    int confirmed = menuList(list, rows, listArea(0.45f));
    if (list.selected >= 0 && list.selected < (int)songs.size() && songs[list.selected].error.empty()) drawSongCard(songs[list.selected], s);
    if (confirmed >= 0 && confirmed < (int)songs.size()){
        // Several parts to play: choose one first
        if (!forEditing && songs[confirmed].parts.size() > 1) choosingPartOf = confirmed;
        else choice.songIndex = confirmed;
    }
    if (confirmed == newSong) choice.newSong = true;
    if (confirmed == openFolder) choice.openDataFolder = true;
    if (confirmed == back) choice.back = true;

    if (!error.empty() || !notice.empty()){
        ImGui::SetCursorPos(ImVec2(ImGui::GetWindowWidth() * 0.07f, ImGui::GetWindowHeight() * 0.17f + 20 * s));
        if (!error.empty()) ImGui::TextColored(uiColorVec(UiColor::Bad), "%s", error.c_str());
        else ImGui::TextColored(uiColorVec(UiColor::Good), "%s", notice.c_str());
    }
    menuScreenHint(forEditing ? "Up/Down  choose    Enter  edit    Esc  back    Drop a .lahn file to add a song"
                              : "Up/Down  choose    Enter  play    Esc  back    Drop a .lahn file to add a song", s);
    ImGui::End();
    return choice;
}

PauseChoice pauseScreen(const std::string& song){
    static MenuList list;
    static const std::vector<MenuRow> rows = { actionRow("Resume", "Esc"), actionRow("Retry"), actionRow("Quit to songs") };
    PauseChoice choice = PauseChoice::None;
    beginMenu("Paused");
    float s = menuScale(), width = ImGui::GetWindowWidth(), height = ImGui::GetWindowHeight();
    if (ImGui::IsWindowAppearing()) list.selected = 0; // Resume first, every time
    // The play screen stays in sight, dimmed behind the menu
    ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(0, 0), ImVec2(width, height), uiColor(UiColor::Background, 0.88f));
    menuScreenTitle("Paused", s);
    ImGui::GetWindowDrawList()->AddText(uiFonts().text, 18 * s, ImVec2(width * 0.07f, height * 0.09f + 50 * s), uiColor(UiColor::Dim), song.c_str());
    int confirmed = menuList(list, rows, {ImVec2(width * 0.07f, height * 0.25f), width * 0.45f, 3 * 48 * s, s});
    if (confirmed == 0) choice = PauseChoice::Resume;
    if (confirmed == 1) choice = PauseChoice::Retry;
    if (confirmed == 2) choice = PauseChoice::Quit;
    menuScreenHint("Enter  choose    Esc  resume", s);
    ImGui::End();
    return choice;
}

ResultsChoice resultsScreen(const GameResult& result){
    static MenuList list;
    static const std::vector<MenuRow> rows = { actionRow("Retry"), actionRow("Back to songs", "Esc") };
    ResultsChoice choice = ResultsChoice::None;
    Grade grade = gradeFor(result.accuracy, result.missCount);

    beginMenu("Results");
    float s = menuScale(), width = ImGui::GetWindowWidth(), height = ImGui::GetWindowHeight();
    std::string title = result.title + (result.partName.empty() ? "" : "  ·  " + result.partName);
    menuScreenTitle(title.c_str(), s);
    if (ImGui::IsWindowAppearing()) list.selected = 0; // Retry first, every time

    ImDrawList* draw = ImGui::GetWindowDrawList();
    const UiFonts& fonts = uiFonts();
    auto textWidth = [](ImFont* font, float size, const char* text){ return font ? font->CalcTextSizeA(size, FLT_MAX, 0.0f, text).x : size * 3; };

    // The run, on a card: the grade big, the accuracy by it, then the score, the combo and the timing
    ImVec2 card(width * 0.55f, height * 0.25f);
    float cardWidth = width * 0.38f, pad = 26 * s, inner = cardWidth - 2 * pad;
    float cardHeight = pad * 2 + 110 * s + 3 * (13 * s + 8 * s + 30 * s + 14 * s) + 22 * s;
    draw->AddRectFilled(ImVec2(card.x, card.y + 3 * s), ImVec2(card.x + cardWidth, card.y + cardHeight + 3 * s), uiColor(UiColor::Ink, 0.04f), 10 * s);
    draw->AddRectFilled(card, ImVec2(card.x + cardWidth, card.y + cardHeight), uiColor(UiColor::Card), 10 * s);
    float x = card.x + pad, y = card.y + pad;
    const char* gradeText = gradeName(grade);
    draw->AddText(fonts.heavy, 100 * s, ImVec2(x - 4 * s, y - 14 * s), uiColor(gradeColor(grade)), gradeText);
    float right = x + textWidth(fonts.heavy, 100 * s, gradeText) + 24 * s;
    draw->AddText(fonts.heavy, 46 * s, ImVec2(right, y + 4 * s), uiColor(UiColor::Ink), TextFormat("%.2f%%", result.accuracy));
    const char* standing = result.place == 0 ? "NEW BEST" : (result.place > 0 ? TextFormat("YOUR #%d RUN", result.place + 1) : "");
    draw->AddText(fonts.mono, 14 * s, ImVec2(right, y + 60 * s), uiColor(result.place == 0 ? UiColor::Accent : UiColor::Dim), standing);
    y += 110 * s;
    auto row = [&](const char* label, const std::string& value, UiColor color, const std::string& detail){
        draw->AddText(fonts.mono, 13 * s, ImVec2(x, y), uiColor(UiColor::Dim), label);
        y += 13 * s + 8 * s;
        draw->AddText(fonts.heavy, 30 * s, ImVec2(x, y), uiColor(color), value.c_str());
        if (!detail.empty()){
            float valueWidth = textWidth(fonts.heavy, 30 * s, value.c_str());
            draw->AddText(fonts.text, 17 * s, ImVec2(x + valueWidth + 12 * s, y + 9 * s), uiColor(UiColor::Dim), detail.c_str(), nullptr, inner);
        }
        y += 30 * s + 14 * s;
    };
    row("SCORE", TextFormat("%d", result.score), UiColor::Ink, "");
    bool fullCombo = result.missCount == 0 && result.totalNotes > 0;
    row("BEST COMBO", TextFormat("%d", result.maxCombo), fullCombo ? UiColor::Accent : UiColor::Ink,
        fullCombo ? "FULL COMBO" : TextFormat("of %d notes", result.totalNotes));
    const TimingStats& timing = result.timing;
    std::string lean = std::fabs(timing.meanMs) < 1.0f ? "right on average"
                     : TextFormat("%.0f ms %s on average", std::fabs(timing.meanMs), timing.meanMs > 0 ? "early" : "late");
    row("TIMING", TextFormat("%.0f UR", timing.unstableRate), UiColor::Ink, lean);
    draw->AddText(fonts.text, 17 * s, ImVec2(x, y), uiColor(UiColor::Dim),
                  TextFormat("Perfect %d  ·  Good %d  ·  Miss %d%s", result.perfectCount, result.nearCount, result.missCount,
                             result.withInstrument ? "" : "  ·  keyboard"));

    // Retry and Back, and under them the part's best runs, this one highlighted
    float left = width * 0.07f, listTop = height * 0.25f;
    int confirmed = menuList(list, rows, {ImVec2(left, listTop), width * 0.4f, 2 * 48 * s, s});
    if (confirmed == 0) choice = ResultsChoice::Retry;
    if (confirmed == 1) choice = ResultsChoice::BackToSongs;
    if (!result.records.empty()){
        float boardY = listTop + 2 * 48 * s + 36 * s;
        draw->AddText(fonts.mono, 13 * s, ImVec2(left, boardY), uiColor(UiColor::Dim), "YOUR BEST RUNS");
        boardY += 13 * s + 12 * s;
        for (int i = 0; i < (int)result.records.size() && i < 6; i++){
            const RunRecord& run = result.records[i];
            bool thisRun = i == result.place;
            ImU32 ink = uiColor(thisRun ? UiColor::Accent : UiColor::Ink), dim = uiColor(thisRun ? UiColor::Accent : UiColor::Dim);
            draw->AddText(fonts.mono, 15 * s, ImVec2(left, boardY + 3 * s), dim, TextFormat("%d", i + 1));
            draw->AddText(fonts.heavy, 20 * s, ImVec2(left + 30 * s, boardY), uiColor(thisRun ? UiColor::Accent : gradeColor(run.grade())), gradeName(run.grade()));
            draw->AddText(fonts.bold, 20 * s, ImVec2(left + 72 * s, boardY), ink, TextFormat("%d", run.score));
            draw->AddText(fonts.text, 17 * s, ImVec2(left + 180 * s, boardY + 2 * s), dim,
                          TextFormat("%.2f%%  ·  %s%s  ·  %s", run.accuracy, run.fullCombo() ? "FC" : TextFormat("%dx", run.maxCombo),
                                     run.withInstrument ? "" : "  ·  keys", run.date.c_str()));
            boardY += 30 * s;
        }
    }
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
