#include "screens/importsong.h"

#include "app/filedialog.h"
#include "core/guitarpro.h"
#include "core/music.h"
#include "core/songlibrary.h"
#include "imgui.h"
#include "raylib.h"
#include "ui/menulist.h"
#include "ui/settingsui.h"
#include "ui/theme.h"
#include "ui/ui.h"

#include <algorithm>
#include <cctype>
#include <filesystem>

namespace fs = std::filesystem;

const int BACKING_RATE = 44100;
const char* const TAB_PATTERNS[] = { "*.gp", "*.gpx", "*.gp5", "*.gp4", "*.gp3" };
const char* const AUDIO_PATTERNS[] = { "*.mp3", "*.ogg", "*.flac", "*.wav" };

static struct {
    std::string songsDir;
    std::string file;           // the tab
    bool loaded = false;
    GuitarProImport import;
    std::string audio;          // the song's recording, "" for lahn's backing
    bool useRecording = false;
    std::string error;
    int importing = 0;          // frames since Import was pressed: the work waits a frame, so "Importing..." shows
    std::string title;          // what it was imported as
} importView;

static std::string lower(std::string text){
    for (char& c : text) c = (char)std::tolower((unsigned char)c);
    return text;
}

static bool isTab(const std::string& path){
    std::string extension = lower(fs::path(path).extension().string());
    for (const char* pattern : TAB_PATTERNS) if (extension == pattern + 1) return true;
    return false;
}

static bool isAudio(const std::string& path){
    std::string extension = lower(fs::path(path).extension().string());
    for (const char* pattern : AUDIO_PATTERNS) if (extension == pattern + 1) return true;
    return false;
}

static void loadTab(const std::string& path){
    importView.file = path;
    importView.error.clear();
    importView.loaded = importGuitarPro(path, importView.import, importView.error);
    if (!importView.loaded) importView.error = fs::path(path).filename().string() + ": " + importView.error;
}

void openImportScreen(const std::string& songsDir, const std::string& file){
    importView = {};
    importView.songsDir = songsDir;
    if (!file.empty()) loadTab(file);
}

void closeImportScreen(){
    importView.import = GuitarProImport{};
    importView.loaded = false;
}

std::string importedSongTitle(){
    return importView.title;
}

// Files dropped on the window: a tab to import, or the song's recording
static void takeDropped(){
    if (!IsFileDropped()) return;
    FilePathList dropped = LoadDroppedFiles();
    for (unsigned i = 0; i < dropped.count; i++){
        std::string path = dropped.paths[i];
        if (isTab(path)) loadTab(path);
        else if (isAudio(path) && importView.loaded){
            importView.audio = path;
            importView.useRecording = true;
        } else {
            importView.error = fs::path(path).filename().string() + " is neither a Guitar Pro tab nor an audio file";
        }
    }
    UnloadDroppedFiles(dropped);
}

static std::vector<std::string> patterns(const char* const* list, int count){
    return std::vector<std::string>(list, list + count);
}

// "E A D G", lowest string first
static std::string tuningName(const std::vector<int>& tuning){
    std::string name;
    for (int pitch : tuning) name += (name.empty() ? "" : " ") + std::string(pitchClassName(pitch));
    return name;
}

// A choice among two, drawn as a card: its circle filled when it's the one
static bool optionCard(const char* id, ImVec2 min, ImVec2 max, bool chosen, const char* title, const char* detail, float s){
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const UiFonts& fonts = uiFonts();
    ImGui::SetCursorScreenPos(min);
    bool clicked = ImGui::InvisibleButton(id, ImVec2(max.x - min.x, max.y - min.y));
    bool hovered = ImGui::IsItemHovered();
    if (hovered) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    draw->AddRectFilled(min, max, uiColor(UiColor::Card), 12 * s);
    draw->AddRect(min, max, uiColor(chosen ? UiColor::Accent : UiColor::StaffLine, chosen || hovered ? 1.0f : 0.8f), 12 * s, 0, (chosen ? 2.0f : 1.2f) * s);
    ImVec2 dot(min.x + 24 * s, min.y + 28 * s);
    draw->AddCircle(dot, 8 * s, uiColor(chosen ? UiColor::Accent : UiColor::Dim), 24, 2.0f * s);
    if (chosen) draw->AddCircleFilled(dot, 4.5f * s, uiColor(UiColor::Accent), 24);
    draw->AddText(fonts.bold, 17 * s, ImVec2(min.x + 44 * s, min.y + 18 * s), uiColor(UiColor::Ink), title);
    draw->AddText(fonts.text, 14 * s, ImVec2(min.x + 44 * s, min.y + 42 * s), uiColor(UiColor::Dim), detail, nullptr, max.x - min.x - 60 * s);
    return clicked;
}

ImportChoice importScreen(){
    ImportChoice choice = ImportChoice::None;
    takeDropped();
    beginMenu("Import a song");
    const float s = menuScale(), width = ImGui::GetWindowWidth(), height = ImGui::GetWindowHeight();
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const UiFonts& fonts = uiFonts();
    const float left = width * 0.07f, right = width * 0.93f, top = height * 0.09f + 94 * s;
    const float controlHeight = settingsControlHeight();
    menuScreenTitle("Import a song", s);
    draw->AddText(fonts.text, 18 * s, ImVec2(left, height * 0.09f + 52 * s), uiColor(UiColor::Dim),
                  "A Guitar Pro tab: its guitar and bass parts become a song to play.");

    auto chooseTab = [&]{
        std::string path, error;
        if (chooseFile("Choose a Guitar Pro tab", "Guitar Pro tabs", patterns(TAB_PATTERNS, 5), path, error)) loadTab(path);
        else if (!error.empty()) importView.error = error;
    };

    if (!importView.loaded){
        // Nothing yet: a place to drop it, and the dialog
        ImVec2 min(left, top + 10 * s), max(right, top + 250 * s);
        draw->AddRectFilled(min, max, uiColor(UiColor::Card, 0.6f), 16 * s);
        for (float x = min.x + 8 * s; x < max.x - 8 * s; x += 14 * s){ // a dashed edge, for dropping onto
            draw->AddLine(ImVec2(x, min.y), ImVec2(std::min(x + 7 * s, max.x), min.y), uiColor(UiColor::Dim, 0.5f), 1.5f * s);
            draw->AddLine(ImVec2(x, max.y), ImVec2(std::min(x + 7 * s, max.x), max.y), uiColor(UiColor::Dim, 0.5f), 1.5f * s);
        }
        const char* drop = "Drop a Guitar Pro file here";
        float dropWidth = fonts.bold ? fonts.bold->CalcTextSizeA(24 * s, FLT_MAX, 0.0f, drop).x : 300 * s;
        draw->AddText(fonts.bold, 24 * s, ImVec2((min.x + max.x - dropWidth) / 2, min.y + 60 * s), uiColor(UiColor::Ink), drop);
        const char* kinds = ".gp from Guitar Pro 7 and 8, the most common now. Older ones (.gpx, .gp5) are on their way.";
        float kindsWidth = fonts.text ? fonts.text->CalcTextSizeA(15 * s, FLT_MAX, 0.0f, kinds).x : 400 * s;
        draw->AddText(fonts.text, 15 * s, ImVec2((min.x + max.x - kindsWidth) / 2, min.y + 100 * s), uiColor(UiColor::Dim), kinds);
        float buttonWidth = 180 * s, buttonX = (min.x + max.x - buttonWidth) / 2;
        if (settingsButtonAt("choose", ImVec2(buttonX, min.y + 150 * s), ImVec2(buttonX + buttonWidth, min.y + 150 * s + controlHeight), "Choose a file...")) chooseTab();
        if (!importView.error.empty()) draw->AddText(fonts.text, 16 * s, ImVec2(left, max.y + 20 * s), uiColor(UiColor::Bad), importView.error.c_str(), nullptr, right - left);
        menuScreenHint("Drop a tab on the window, or choose one    Esc  back", s);
        ImGui::End();
        return choice;
    }

    // The tab: its title, and each part coming in
    const Chart& chart = importView.import.chart;
    const float columnWidth = (right - left - 40 * s) / 2;
    float x = left, y = top + 10 * s;
    ImVec2 cardMin(x, y);
    float cardHeight = 110 * s + chart.frettedTracks.size() * 54 * s + (importView.import.leftOut.empty() ? 0 : 44 * s);
    draw->AddRectFilled(ImVec2(cardMin.x, cardMin.y + 3 * s), ImVec2(x + columnWidth, y + cardHeight + 3 * s), uiColor(UiColor::Ink, 0.04f), 12 * s);
    draw->AddRectFilled(cardMin, ImVec2(x + columnWidth, y + cardHeight), uiColor(UiColor::Card), 12 * s);
    float pad = 22 * s, inner = columnWidth - 2 * pad;
    std::string title = chart.title.empty() ? fs::path(importView.file).stem().string() : chart.title;
    draw->AddText(fonts.bold, 24 * s, ImVec2(x + pad, y + pad), uiColor(UiColor::Ink), title.c_str(), nullptr, inner);
    draw->AddText(fonts.text, 16 * s, ImVec2(x + pad, y + pad + 32 * s), uiColor(UiColor::Dim),
                  chart.artist.empty() ? fs::path(importView.file).filename().string().c_str() : chart.artist.c_str(), nullptr, inner);
    float rowY = y + pad + 70 * s;
    for (const FrettedTrack& track : chart.frettedTracks){
        bool bass = track.type == InstrumentType::Bass;
        draw->AddText(fonts.mono, 12 * s, ImVec2(x + pad, rowY), uiColor(UiColor::Dim),
                      TextFormat("%s  ·  %d STRINGS  ·  %s", bass ? "BASS" : "GUITAR", (int)track.tuning.size(), tuningName(track.tuning).c_str()));
        draw->AddText(fonts.bold, 18 * s, ImVec2(x + pad, rowY + 18 * s), uiColor(UiColor::Ink), track.name.c_str(), nullptr, inner - 120 * s);
        const char* count = TextFormat("%d notes", (int)track.notes.size());
        float countWidth = fonts.text ? fonts.text->CalcTextSizeA(15 * s, FLT_MAX, 0.0f, count).x : 60 * s;
        draw->AddText(fonts.text, 15 * s, ImVec2(x + pad + inner - countWidth, rowY + 20 * s), uiColor(UiColor::Dim), count);
        rowY += 54 * s;
    }
    if (!importView.import.leftOut.empty()){
        std::string leftOut = "Left out: ";
        for (size_t i = 0; i < importView.import.leftOut.size(); i++) leftOut += (i ? ", " : "") + importView.import.leftOut[i];
        draw->AddText(fonts.text, 14 * s, ImVec2(x + pad, rowY + 4 * s), uiColor(UiColor::Dim), leftOut.c_str(), nullptr, inner);
    }
    if (settingsButtonAt("another", ImVec2(x, y + cardHeight + 16 * s), ImVec2(x + 200 * s, y + cardHeight + 16 * s + controlHeight), "Another file...")) chooseTab();

    // Its audio: the song's recording, or lahn's backing
    x = left + columnWidth + 40 * s;
    draw->AddText(fonts.mono, 13 * s, ImVec2(x, y), uiColor(UiColor::Dim), "WHAT YOU PLAY ALONG TO");
    y += 26 * s;
    if (optionCard("backing", ImVec2(x, y), ImVec2(x + columnWidth, y + 96 * s), !importView.useRecording, "lahn's backing",
                   "Every part on lahn's synths, with a click on the beat. In time from the start: play it right away.", s)){
        importView.useRecording = false;
    }
    y += 110 * s;
    std::string recordingDetail = importView.audio.empty() ? "Its mp3, ogg, flac or wav: choose it, or drop it on the window. Then line it up in the song editor."
                                                           : fs::path(importView.audio).filename().string() + ". Line it up in the song editor once it's in.";
    if (optionCard("recording", ImVec2(x, y), ImVec2(x + columnWidth, y + 96 * s), importView.useRecording, "The song's recording", recordingDetail.c_str(), s)){
        importView.useRecording = true;
    }
    y += 110 * s;
    auto chooseAudio = [&]{
        std::string path, error;
        if (chooseFile("Choose the song's audio", "Audio", patterns(AUDIO_PATTERNS, 4), path, error)){
            importView.audio = path;
            importView.useRecording = true;
        } else if (!error.empty()) importView.error = error;
    };
    if (importView.useRecording && settingsButtonAt("audio", ImVec2(x, y), ImVec2(x + 200 * s, y + controlHeight),
                                                    importView.audio.empty() ? "Choose audio..." : "Other audio...")) chooseAudio();

    // Import: the work waits a frame, so what's happening shows first
    bool ready = !importView.useRecording || !importView.audio.empty();
    float buttonY = height - 64 * s - controlHeight - 20 * s;
    ImGui::BeginDisabled(!ready || importView.importing > 0);
    if (settingsButtonAt("import", ImVec2(right - 220 * s, buttonY), ImVec2(right, buttonY + controlHeight),
                         importView.importing > 0 ? "Importing..." : "Import")) importView.importing = 1;
    ImGui::EndDisabled();
    if (importView.importing > 0 && ++importView.importing > 2){
        std::string chartPath, error;
        if (createImportedSong(importView.songsDir, chart, importView.useRecording ? importView.audio : "", BACKING_RATE, chartPath, error)){
            importView.title = title;
            choice = ImportChoice::Imported;
        } else {
            importView.error = error;
        }
        importView.importing = 0;
    }
    if (!importView.error.empty()) draw->AddText(fonts.text, 16 * s, ImVec2(left, buttonY + 8 * s), uiColor(UiColor::Bad), importView.error.c_str(), nullptr, right - left - 240 * s);
    menuScreenHint("Drop a tab or its audio on the window    Esc  back", s);
    ImGui::End();
    return choice;
}
