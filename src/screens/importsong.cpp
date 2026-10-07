#include "screens/importsong.h"

#include "app/filedialog.h"
#include "app/stemmodel.h"
#include "audio/audio.h"
#include "core/addon.h"
#include "core/backing.h"
#include "core/guitarpro.h"
#include "core/music.h"
#include "core/songlibrary.h"
#include "core/stemsplit.h"
#include "core/transcribe.h"
#include "imgui.h"
#include "raylib.h"
#include "ui/menulist.h"
#include "ui/settingsui.h"
#include "ui/theme.h"
#include "ui/ui.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <thread>

namespace fs = std::filesystem;

const int BACKING_RATE = 44100;
const int LISTEN_RATE = 44100;   // a recording is heard at this rate, whatever its own
const char* const TAB_PATTERNS[] = { "*.gp", "*.gpx", "*.gp5", "*.gp4", "*.gp3" };
const char* const AUDIO_PATTERNS[] = { "*.mp3", "*.ogg", "*.flac", "*.wav" };

// Where a song comes from: a tab; a recording of its bass alone, written down; or the song itself, its bass taken
// out of it first (the stems add-on)
enum class Source { Tab, Recording, Song };
const char* const ADDON_PATTERNS[] = { "*.lahnaddon" };

static struct ImportState {
    std::string songsDir;
    std::string addonsDir;
    Source source = Source::Tab;
    // From a tab
    std::string file;           // the tab
    bool loaded = false;
    GuitarProImport import;
    std::string audio;          // the song's recording, "" for lahn's backing
    bool useRecording = false;
    // From a recording
    std::string recording;      // the bass alone
    bool heard = false;         // written down
    Transcription transcription;
    std::string wholeSong;      // the song it came from, to play along to instead of the bass alone
    bool useWholeSong = false;
    // From a song (the stems add-on): `transcription` is its bass, taken out and written down
    std::string song;
    bool split = false;
    int playAlong = 0;          // 0 the whole song, 1 the song without its bass, 2 its bass alone
    std::string notice;         // good news: the add-on installed
    // Both
    std::string error;
    int importing = 0;          // frames since Import was pressed: the work waits a frame, so "Importing..." shows
    std::string title;          // what it was imported as
} importView;

// Listening to a recording takes a few seconds: it's done on a thread of its own, and taken up when it's done
static std::thread worker;
static std::atomic<bool> working{false}, cancelWork{false}, workDone{false};
static std::mutex workLock;
static Transcription workResult;
static std::string workError;
static bool workOk = false;
static std::atomic<int> workStage{0};        // splitting a song: 0 reading it, 1 taking the bass out, 2 writing it down
static std::atomic<float> workProgress{0.0f}; // of taking the bass out
static std::vector<float> songBass, songRest; // the song split: its bass, and the song without it (stereo, at STEM_RATE)

static void stopWorker(){
    cancelWork = true;
    if (worker.joinable()) worker.join();
    cancelWork = false;
    working = false;
    workDone = false;
}

static std::string lower(std::string text){
    for (char& c : text) c = (char)std::tolower((unsigned char)c);
    return text;
}

static bool hasExtension(const std::string& path, const char* const* patterns, int count){
    std::string extension = lower(fs::path(path).extension().string());
    for (int i = 0; i < count; i++) if (extension == patterns[i] + 1) return true;
    return false;
}
static bool isTab(const std::string& path){ return hasExtension(path, TAB_PATTERNS, 5); }
static bool isAudio(const std::string& path){ return hasExtension(path, AUDIO_PATTERNS, 4); }

static void loadTab(const std::string& path){
    importView.source = Source::Tab;
    importView.file = path;
    importView.error.clear();
    importView.loaded = importGuitarPro(path, importView.import, importView.error);
    if (!importView.loaded) importView.error = fs::path(path).filename().string() + ": " + importView.error;
}

// A recording of a bass alone: heard, and its bass line written down, on the worker
static void listenTo(const std::string& path){
    stopWorker();
    importView.source = Source::Recording;
    importView.recording = path;
    importView.heard = false;
    importView.wholeSong.clear();
    importView.useWholeSong = false;
    importView.error.clear();
    working = true;
    std::string title = fs::path(path).stem().string();
    worker = std::thread([path, title]{
        std::vector<float> samples;
        std::string error;
        Transcription heard;
        bool ok = decodeAudioFile(path, LISTEN_RATE, 1, samples, error, cancelWork) && transcribeBass(samples, LISTEN_RATE, title, heard, error);
        {
            std::lock_guard<std::mutex> lock(workLock);
            workResult = heard;
            workError = error;
            workOk = ok;
        }
        workDone = true;
        working = false;
    });
}

// A whole song: its bass taken out of it by the stems add-on's model, then written down, on the worker
static void splitSong(const std::string& path){
    stopWorker();
    importView.source = Source::Song;
    importView.song = path;
    importView.split = false;
    importView.playAlong = 0;
    importView.error.clear();
    if (!stemsAddonInstalled(importView.addonsDir)) return; // the screen says what's needed; the song is kept for after
    working = true;
    workStage = 0;
    workProgress = 0.0f;
    std::string title = fs::path(path).stem().string(), addonsDir = importView.addonsDir;
    worker = std::thread([path, title, addonsDir]{
        std::vector<float> stereo, left, right, bassLeft, bassRight, bass, rest;
        std::string error;
        Transcription heard;
        bool ok = decodeAudioFile(path, STEM_RATE, 2, stereo, error, cancelWork);
        if (ok){
            left.resize(stereo.size() / 2);
            right.resize(stereo.size() / 2);
            for (size_t i = 0; i < left.size(); i++){ left[i] = stereo[2 * i]; right[i] = stereo[2 * i + 1]; }
            workStage = 1;
            ok = openStemModel(addonsDir, error) && splitStem(left, right, runStemModel, bassLeft, bassRight, &workProgress, cancelWork, error);
            if (!ok && !stemModelError().empty()) error = stemModelError();
            closeStemModel(); // its memory given back: it's a big model
        }
        if (ok){
            workStage = 2;
            std::vector<float> mono(bassLeft.size());
            for (size_t i = 0; i < mono.size(); i++) mono[i] = 0.5f * (bassLeft[i] + bassRight[i]);
            ok = transcribeBass(mono, STEM_RATE, title, heard, error);
        }
        if (ok){
            bass.resize(stereo.size());
            rest.resize(stereo.size());
            for (size_t i = 0; i < bassLeft.size(); i++){
                bass[2 * i] = bassLeft[i];
                bass[2 * i + 1] = bassRight[i];
                rest[2 * i] = left[i] - bassLeft[i];
                rest[2 * i + 1] = right[i] - bassRight[i];
            }
        }
        {
            std::lock_guard<std::mutex> lock(workLock);
            workResult = heard;
            workError = error;
            workOk = ok;
            songBass = bass;
            songRest = rest;
        }
        workDone = true;
        working = false;
    });
}

// The stems add-on, from its file: installed into the add-ons folder; a song waiting for it is split
static void installStems(const std::string& path){
    AddonInfo info;
    std::string error;
    if (!installAddon(path, importView.addonsDir, info, error)){
        importView.error = error;
        return;
    }
    importView.error.clear();
    importView.notice = "The " + info.name + " add-on is installed.";
    importView.source = Source::Song;
    if (!importView.song.empty() && !importView.split && !working) splitSong(importView.song);
}

void openImportScreen(const std::string& songsDir, const std::string& addonsDir, const std::string& file){
    stopWorker();
    importView = ImportState{};
    importView.songsDir = songsDir;
    importView.addonsDir = addonsDir;
    if (hasExtension(file, ADDON_PATTERNS, 1)) installStems(file);
    else if (isAudio(file)) splitSong(file); // a song, most likely: the screen says so if it needs the add-on
    else if (!file.empty()) loadTab(file);
}

void closeImportScreen(){
    stopWorker();
    importView.import = GuitarProImport{};
    importView.transcription = Transcription{};
    importView.loaded = importView.heard = importView.split = false;
    songBass = {};
    songRest = {};
}

std::string importedSongTitle(){
    return importView.title;
}

// Files dropped on the window: a tab, a recording of a bass, or the song a recording goes with
static void takeDropped(){
    if (!IsFileDropped()) return;
    FilePathList dropped = LoadDroppedFiles();
    for (unsigned i = 0; i < dropped.count; i++){
        std::string path = dropped.paths[i];
        if (isTab(path)) loadTab(path);
        else if (hasExtension(path, ADDON_PATTERNS, 1)) installStems(path);
        else if (isAudio(path)){
            if (importView.source == Source::Tab && importView.loaded){ importView.audio = path; importView.useRecording = true; }
            else if (importView.source == Source::Recording && importView.heard){ importView.wholeSong = path; importView.useWholeSong = true; }
            else if (importView.source == Source::Recording) listenTo(path);
            else splitSong(path);
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

static float textWidth(ImFont* font, float size, const char* text){
    return font ? font->CalcTextSizeA(size, FLT_MAX, 0.0f, text).x : size * 0.5f * (float)std::strlen(text);
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

// A place to drop a file, with a button for the dialog; true when the button's pressed
static bool dropZone(ImVec2 min, ImVec2 max, const char* headline, const char* detail, float s){
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const UiFonts& fonts = uiFonts();
    draw->AddRectFilled(min, max, uiColor(UiColor::Card, 0.6f), 16 * s);
    for (float x = min.x + 8 * s; x < max.x - 8 * s; x += 14 * s){ // a dashed edge, for dropping onto
        draw->AddLine(ImVec2(x, min.y), ImVec2(std::min(x + 7 * s, max.x), min.y), uiColor(UiColor::Dim, 0.5f), 1.5f * s);
        draw->AddLine(ImVec2(x, max.y), ImVec2(std::min(x + 7 * s, max.x), max.y), uiColor(UiColor::Dim, 0.5f), 1.5f * s);
    }
    draw->AddText(fonts.bold, 24 * s, ImVec2((min.x + max.x - textWidth(fonts.bold, 24 * s, headline)) / 2, min.y + 56 * s), uiColor(UiColor::Ink), headline);
    float detailWidth = std::min(max.x - min.x - 80 * s, textWidth(fonts.text, 15 * s, detail));
    draw->AddText(fonts.text, 15 * s, ImVec2((min.x + max.x - detailWidth) / 2, min.y + 96 * s), uiColor(UiColor::Dim), detail, nullptr, detailWidth + 1);
    float buttonWidth = 180 * s, buttonX = (min.x + max.x - buttonWidth) / 2, controlHeight = settingsControlHeight();
    return settingsButtonAt("choose", ImVec2(buttonX, max.y - 40 * s - controlHeight), ImVec2(buttonX + buttonWidth, max.y - 40 * s), "Choose a file...");
}

// A card's frame, its shadow under it
static void card(ImVec2 min, ImVec2 max, float s){
    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(ImVec2(min.x, min.y + 3 * s), ImVec2(max.x, max.y + 3 * s), uiColor(UiColor::Ink, 0.04f), 12 * s);
    draw->AddRectFilled(min, max, uiColor(UiColor::Card), 12 * s);
}

// The bass line heard, small: each note a dash at its time and pitch, so it can be seen that something was heard
static void drawLine(const Chart& chart, ImVec2 min, ImVec2 max, float s){
    ImDrawList* draw = ImGui::GetWindowDrawList();
    if (chart.frettedTracks.empty() || chart.endTick <= 0) return;
    const FrettedTrack& bass = chart.frettedTracks[0];
    int lowest = 127, highest = 0;
    for (const FrettedNote& note : bass.notes){
        int pitch = bass.tuning[note.stringIndex] + note.fret;
        lowest = std::min(lowest, pitch);
        highest = std::max(highest, pitch);
    }
    float range = (float)std::max(12, highest - lowest);
    for (const FrettedNote& note : bass.notes){
        int pitch = bass.tuning[note.stringIndex] + note.fret;
        float x0 = min.x + (max.x - min.x) * note.tick / chart.endTick;
        float x1 = std::max(x0 + 2 * s, min.x + (max.x - min.x) * (note.tick + note.duration) / chart.endTick);
        float y = max.y - (max.y - min.y) * (pitch - lowest) / range;
        draw->AddRectFilled(ImVec2(x0, y - 1.5f * s), ImVec2(x1, y + 1.5f * s), uiColor(UiColor::Accent, 0.85f), 1.5f * s);
    }
}

// FROM: A TAB, A RECORDING, beside the title
static void drawSourceSwitch(float s){
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const UiFonts& fonts = uiFonts();
    float x = ImGui::GetWindowWidth() * 0.55f, y = ImGui::GetWindowHeight() * 0.09f + 14 * s;
    draw->AddText(fonts.mono, 13 * s, ImVec2(x, y + 4 * s), uiColor(UiColor::Dim), "FROM");
    x += 100 * s;
    const char* names[3] = { "A TAB", "A BASS RECORDING", "A SONG" };
    for (int i = 0; i < 3; i++){
        bool on = (int)importView.source == i;
        float w = textWidth(fonts.bold, 20 * s, names[i]);
        ImGui::SetCursorScreenPos(ImVec2(x, y));
        bool clicked = ImGui::InvisibleButton(names[i], ImVec2(w, 24 * s));
        bool hovered = ImGui::IsItemHovered();
        if (hovered && !on) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        draw->AddText(fonts.bold, 20 * s, ImVec2(x, y), uiColor(on || hovered ? UiColor::Ink : UiColor::Dim), names[i]);
        if (on) draw->AddRectFilled(ImVec2(x, y + 25 * s), ImVec2(x + w, y + 27 * s), uiColor(UiColor::Accent));
        if (clicked && !on && !working){ // not while it's listening: that's for the source it's on
            importView.source = (Source)i;
            importView.error.clear();
        }
        x += w + 22 * s;
    }
}

// Import, at the bottom right: the work waits a frame, so "Importing..." shows first. True on the frame to do it.
static bool importButton(bool ready, float s){
    const float width = ImGui::GetWindowWidth(), height = ImGui::GetWindowHeight(), controlHeight = settingsControlHeight();
    float buttonY = height - 64 * s - controlHeight - 20 * s, right = width * 0.93f;
    ImGui::BeginDisabled(!ready || importView.importing > 0);
    if (settingsButtonAt("import", ImVec2(right - 220 * s, buttonY), ImVec2(right, buttonY + controlHeight),
                         importView.importing > 0 ? "Importing..." : "Import")) importView.importing = 1;
    ImGui::EndDisabled();
    if (!importView.error.empty()){
        ImGui::GetWindowDrawList()->AddText(uiFonts().text, 16 * s, ImVec2(width * 0.07f, buttonY + 8 * s), uiColor(UiColor::Bad),
                                            importView.error.c_str(), nullptr, right - width * 0.07f - 240 * s);
    }
    return importView.importing > 0 && ++importView.importing > 2;
}

static void finishImport(const Chart& chart, const std::string& audio, const std::string& title, ImportChoice& choice){
    std::string chartPath, error;
    if (createImportedSong(importView.songsDir, chart, audio, BACKING_RATE, chartPath, error)){
        importView.title = title;
        choice = ImportChoice::Imported;
    } else {
        importView.error = error;
    }
    importView.importing = 0;
}

static void tabScreen(ImportChoice& choice, float s){
    const float width = ImGui::GetWindowWidth(), height = ImGui::GetWindowHeight();
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const UiFonts& fonts = uiFonts();
    const float left = width * 0.07f, right = width * 0.93f, top = height * 0.09f + 94 * s, controlHeight = settingsControlHeight();
    auto chooseTab = [&]{
        std::string path, error;
        if (chooseFile("Choose a Guitar Pro tab", "Guitar Pro tabs", patterns(TAB_PATTERNS, 5), path, error)) loadTab(path);
        else if (!error.empty()) importView.error = error;
    };
    if (!importView.loaded){
        if (dropZone(ImVec2(left, top + 10 * s), ImVec2(right, top + 250 * s), "Drop a Guitar Pro file here",
                     "Every Guitar Pro file: .gp (7 and 8), .gpx (6), .gp5, .gp4 and .gp3. Its guitar and bass parts become a song.", s)) chooseTab();
        if (!importView.error.empty()) draw->AddText(fonts.text, 16 * s, ImVec2(left, top + 270 * s), uiColor(UiColor::Bad), importView.error.c_str(), nullptr, right - left);
        return;
    }

    // The tab: its title, and each part coming in
    const Chart& chart = importView.import.chart;
    const float columnWidth = (right - left - 40 * s) / 2;
    float x = left, y = top + 10 * s;
    float cardHeight = 110 * s + chart.frettedTracks.size() * 54 * s + (importView.import.leftOut.empty() ? 0 : 44 * s);
    card(ImVec2(x, y), ImVec2(x + columnWidth, y + cardHeight), s);
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
        draw->AddText(fonts.text, 15 * s, ImVec2(x + pad + inner - textWidth(fonts.text, 15 * s, count), rowY + 20 * s), uiColor(UiColor::Dim), count);
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
    if (importView.useRecording && settingsButtonAt("audio", ImVec2(x, y), ImVec2(x + 200 * s, y + controlHeight),
                                                    importView.audio.empty() ? "Choose audio..." : "Other audio...")){
        std::string path, error;
        if (chooseFile("Choose the song's audio", "Audio", patterns(AUDIO_PATTERNS, 4), path, error)){
            importView.audio = path;
            importView.useRecording = true;
        } else if (!error.empty()) importView.error = error;
    }
    if (importButton(!importView.useRecording || !importView.audio.empty(), s)){
        finishImport(chart, importView.useRecording ? importView.audio : "", title, choice);
    }
}

// What was heard, on a card: the bass line, its tuning, its tempo, its notes, and the line itself, small
static void heardCard(ImVec2 min, float columnWidth, float cardHeight, float s){
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const UiFonts& fonts = uiFonts();
    const Transcription& heard = importView.transcription;
    const Chart& chart = heard.chart;
    const FrettedTrack& bass = chart.frettedTracks[0];
    float x = min.x, y = min.y, pad = 22 * s, inner = columnWidth - 2 * pad;
    card(min, ImVec2(x + columnWidth, y + cardHeight), s);
    draw->AddText(fonts.bold, 24 * s, ImVec2(x + pad, y + pad), uiColor(UiColor::Ink), chart.title.c_str(), nullptr, inner);
    draw->AddText(fonts.mono, 12 * s, ImVec2(x + pad, y + pad + 40 * s), uiColor(UiColor::Dim),
                  TextFormat("BASS  ·  %d STRINGS  ·  %s", (int)bass.tuning.size(), tuningName(bass.tuning).c_str()));
    draw->AddText(fonts.bold, 18 * s, ImVec2(x + pad, y + pad + 60 * s), uiColor(UiColor::Ink),
                  TextFormat("%d notes  ·  about %.0f beats a minute", heard.notes, heard.bpm));
    drawLine(chart, ImVec2(x + pad, y + pad + 104 * s), ImVec2(x + columnWidth - pad, y + cardHeight - pad - 26 * s), s);
    draw->AddText(fonts.text, 14 * s, ImVec2(x + pad, y + cardHeight - pad - 16 * s), uiColor(UiColor::Dim),
                  "A draft: put it right in the song editor once it's in.", nullptr, inner);
}

// The worker done: what it heard taken up; true if it had something
static bool takeWork(const std::string& file, bool& done){
    if (!workDone) return false;
    std::lock_guard<std::mutex> lock(workLock);
    if (worker.joinable()) worker.join();
    workDone = false;
    done = workOk;
    importView.transcription = workResult;
    if (!workOk) importView.error = fs::path(file).filename().string() + ": " + workError;
    return true;
}

static void songScreen(ImportChoice& choice, float s){
    const float width = ImGui::GetWindowWidth(), height = ImGui::GetWindowHeight();
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const UiFonts& fonts = uiFonts();
    const float left = width * 0.07f, right = width * 0.93f, top = height * 0.09f + 94 * s, controlHeight = settingsControlHeight();
    takeWork(importView.song, importView.split);

    if (!stemsAddonInstalled(importView.addonsDir)){
        // The add-on: what it is, and how it's put in
        card(ImVec2(left, top + 10 * s), ImVec2(right, top + 250 * s), s);
        draw->AddText(fonts.bold, 22 * s, ImVec2(left + 28 * s, top + 38 * s), uiColor(UiColor::Ink), "This needs the stems add-on");
        draw->AddText(fonts.text, 16 * s, ImVec2(left + 28 * s, top + 76 * s), uiColor(UiColor::Dim),
                      "Taking the bass out of a song is done by a neural network, too big to give everyone with lahn: it's an add-on of its "
                      "own, free, about 35 MB, and it runs on your computer. Drop its file (lahn-stems...lahnaddon) on this window, or choose it.",
                      nullptr, right - left - 56 * s);
        if (settingsButtonAt("addon", ImVec2(left + 28 * s, top + 180 * s), ImVec2(left + 248 * s, top + 180 * s + controlHeight), "Choose the add-on...")){
            std::string path, error;
            if (chooseFile("Choose lahn's stems add-on", "lahn add-ons", patterns(ADDON_PATTERNS, 1), path, error)) installStems(path);
            else if (!error.empty()) importView.error = error;
        }
        if (!importView.song.empty()){
            draw->AddText(fonts.text, 15 * s, ImVec2(left + 270 * s, top + 188 * s), uiColor(UiColor::Dim),
                          TextFormat("%s waits for it.", fs::path(importView.song).filename().string().c_str()));
        }
        if (!importView.error.empty()) draw->AddText(fonts.text, 16 * s, ImVec2(left, top + 270 * s), uiColor(UiColor::Bad), importView.error.c_str(), nullptr, right - left);
        return;
    }
    auto chooseSong = [&]{
        std::string path, error;
        if (chooseFile("Choose a song", "Audio", patterns(AUDIO_PATTERNS, 4), path, error)) splitSong(path);
        else if (!error.empty()) importView.error = error;
    };
    if (working){
        // What it's doing, and how far along taking the bass out is
        card(ImVec2(left, top + 10 * s), ImVec2(right, top + 190 * s), s);
        int stage = workStage;
        float progress = stage == 0 ? 0.0f : stage == 1 ? workProgress.load() : 1.0f;
        const char* doing = stage == 0 ? "Reading" : stage == 1 ? "Taking the bass out of" : "Writing down the bass of";
        std::string line = std::string(doing) + " " + fs::path(importView.song).filename().string();
        draw->AddText(fonts.bold, 22 * s, ImVec2(left + 28 * s, top + 40 * s), uiColor(UiColor::Ink), line.c_str(), nullptr, right - left - 56 * s);
        draw->AddText(fonts.text, 16 * s, ImVec2(left + 28 * s, top + 80 * s), uiColor(UiColor::Dim),
                      "About a minute for a four-minute song. The game stays open: this happens on your computer, beside it.");
        float barLeft = left + 28 * s, barRight = right - 28 * s, barY = top + 136 * s;
        draw->AddRectFilled(ImVec2(barLeft, barY), ImVec2(barRight, barY + 8 * s), uiColor(UiColor::StaffLine), 4 * s);
        draw->AddRectFilled(ImVec2(barLeft, barY), ImVec2(barLeft + (barRight - barLeft) * std::clamp(progress, 0.02f, 1.0f), barY + 8 * s), uiColor(UiColor::Accent), 4 * s);
        return;
    }
    if (!importView.split){
        if (dropZone(ImVec2(left, top + 10 * s), ImVec2(right, top + 250 * s), "Drop a song here",
                     "Any song (mp3, ogg, flac, wav). lahn takes its bass out of it and writes it down: its notes, rhythm and frets. About a minute for a four-minute song.", s)){
            chooseSong();
        }
        if (!importView.notice.empty()) draw->AddText(fonts.text, 16 * s, ImVec2(left, top + 270 * s), uiColor(UiColor::Good), importView.notice.c_str());
        if (!importView.error.empty()) draw->AddText(fonts.text, 16 * s, ImVec2(left, top + 294 * s), uiColor(UiColor::Bad), importView.error.c_str(), nullptr, right - left);
        return;
    }

    // What was heard, and what to play along to: the song, the song with its bass taken out, or the bass alone
    const float columnWidth = (right - left - 40 * s) / 2;
    float x = left, y = top + 10 * s, cardHeight = 250 * s;
    heardCard(ImVec2(x, y), columnWidth, cardHeight, s);
    if (settingsButtonAt("another", ImVec2(x, y + cardHeight + 16 * s), ImVec2(x + 220 * s, y + cardHeight + 16 * s + controlHeight), "Another song...")) chooseSong();
    x = left + columnWidth + 40 * s;
    draw->AddText(fonts.mono, 13 * s, ImVec2(x, y), uiColor(UiColor::Dim), "WHAT YOU PLAY ALONG TO");
    y += 26 * s;
    const char* titles[3] = { "The whole song", "The song without its bass", "Its bass alone" };
    const char* details[3] = { "As it was recorded: its bass shows you the way.",
                               "The band, and you on the bass: its own bass taken out.",
                               "Only the bass lahn took out: to hear what it wrote down." };
    for (int i = 0; i < 3; i++){
        ImGui::PushID(i);
        if (optionCard("along", ImVec2(x, y), ImVec2(x + columnWidth, y + 74 * s), importView.playAlong == i, titles[i], details[i], s)) importView.playAlong = i;
        ImGui::PopID();
        y += 86 * s;
    }
    if (importButton(true, s)){
        // The song as it is, or what was split from it, written out for the song's folder to take
        std::string audio = importView.song, error;
        if (importView.playAlong != 0){
            audio = (fs::temp_directory_path() / "lahn-import.wav").string();
            if (!writeWav(audio, importView.playAlong == 1 ? songRest : songBass, STEM_RATE, error, 2)){
                importView.error = error;
                importView.importing = 0;
                return;
            }
        }
        finishImport(importView.transcription.chart, audio, importView.transcription.chart.title, choice);
        if (importView.playAlong != 0){
            std::error_code ec;
            fs::remove(audio, ec);
        }
    }
}

static void recordingScreen(ImportChoice& choice, float s){
    const float width = ImGui::GetWindowWidth(), height = ImGui::GetWindowHeight();
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const UiFonts& fonts = uiFonts();
    const float left = width * 0.07f, right = width * 0.93f, top = height * 0.09f + 94 * s, controlHeight = settingsControlHeight();
    auto chooseRecording = [&]{
        std::string path, error;
        if (chooseFile("Choose a recording of a bass alone", "Audio", patterns(AUDIO_PATTERNS, 4), path, error)) listenTo(path);
        else if (!error.empty()) importView.error = error;
    };
    takeWork(importView.recording, importView.heard);

    if (working){
        // Listening: what it's doing, with dots that move
        card(ImVec2(left, top + 10 * s), ImVec2(right, top + 170 * s), s);
        int dots = (int)(GetTime() * 3.0) % 4;
        std::string listening = "Listening to " + fs::path(importView.recording).filename().string() + std::string(dots, '.');
        draw->AddText(fonts.bold, 22 * s, ImVec2(left + 28 * s, top + 44 * s), uiColor(UiColor::Ink), listening.c_str(), nullptr, right - left - 56 * s);
        draw->AddText(fonts.text, 16 * s, ImVec2(left + 28 * s, top + 84 * s), uiColor(UiColor::Dim),
                      "Hearing every note, finding the beat under them, and writing them down on strings and frets. A few seconds.");
        return;
    }
    if (!importView.heard){
        if (dropZone(ImVec2(left, top + 10 * s), ImVec2(right, top + 250 * s), "Drop a recording of a bass alone",
                     "A bass stem split from a song, or you playing the part (mp3, ogg, flac, wav). lahn writes its notes, rhythm and frets down: a draft to put right in the song editor.", s)){
            chooseRecording();
        }
        if (!importView.error.empty()) draw->AddText(fonts.text, 16 * s, ImVec2(left, top + 270 * s), uiColor(UiColor::Bad), importView.error.c_str(), nullptr, right - left);
        return;
    }

    // What was heard: the line, its tempo, its notes
    const Chart& chart = importView.transcription.chart;
    const float columnWidth = (right - left - 40 * s) / 2;
    float x = left, y = top + 10 * s, cardHeight = 250 * s;
    heardCard(ImVec2(x, y), columnWidth, cardHeight, s);
    if (settingsButtonAt("another", ImVec2(x, y + cardHeight + 16 * s), ImVec2(x + 220 * s, y + cardHeight + 16 * s + controlHeight), "Another recording...")){
        chooseRecording();
    }

    // What to play along to: the bass heard, or the whole song it came from
    x = left + columnWidth + 40 * s;
    draw->AddText(fonts.mono, 13 * s, ImVec2(x, y), uiColor(UiColor::Dim), "WHAT YOU PLAY ALONG TO");
    y += 26 * s;
    if (optionCard("alone", ImVec2(x, y), ImVec2(x + columnWidth, y + 96 * s), !importView.useWholeSong, "This recording",
                   "The bass you gave, just as it is: in time with the chart already.", s)){
        importView.useWholeSong = false;
    }
    y += 110 * s;
    std::string wholeDetail = importView.wholeSong.empty() ? "The song the bass came from (its full mix, as long as the bass): choose it, or drop it on the window."
                                                          : fs::path(importView.wholeSong).filename().string() + ": in time too, if the bass was split from it.";
    if (optionCard("whole", ImVec2(x, y), ImVec2(x + columnWidth, y + 96 * s), importView.useWholeSong, "The whole song", wholeDetail.c_str(), s)){
        importView.useWholeSong = true;
    }
    y += 110 * s;
    if (importView.useWholeSong && settingsButtonAt("song", ImVec2(x, y), ImVec2(x + 200 * s, y + controlHeight),
                                                    importView.wholeSong.empty() ? "Choose the song..." : "Another song...")){
        std::string path, error;
        if (chooseFile("Choose the whole song", "Audio", patterns(AUDIO_PATTERNS, 4), path, error)) importView.wholeSong = path;
        else if (!error.empty()) importView.error = error;
    }
    if (importButton(!importView.useWholeSong || !importView.wholeSong.empty(), s)){
        finishImport(chart, importView.useWholeSong ? importView.wholeSong : importView.recording, chart.title, choice);
    }
}

ImportChoice importScreen(){
    ImportChoice choice = ImportChoice::None;
    takeDropped();
    beginMenu("Import a song");
    const float s = menuScale(), height = ImGui::GetWindowHeight();
    menuScreenTitle("Import a song", s);
    ImGui::GetWindowDrawList()->AddText(uiFonts().text, 18 * s, ImVec2(ImGui::GetWindowWidth() * 0.07f, height * 0.09f + 52 * s), uiColor(UiColor::Dim),
                                        importView.source == Source::Tab ? "A Guitar Pro tab: its guitar and bass parts become a song to play."
                                        : importView.source == Source::Recording ? "A bass alone, written down: its notes on the beat, on strings and frets."
                                                                                 : "Any song: its bass taken out of it, and written down.");
    drawSourceSwitch(s);
    if (importView.source == Source::Tab) tabScreen(choice, s);
    else if (importView.source == Source::Recording) recordingScreen(choice, s);
    else songScreen(choice, s);
    menuScreenHint("Drop a tab, a recording or a song on the window    Esc  back", s);
    ImGui::End();
    return choice;
}
