#include "screens/menus.h"

#include "screens/comparison.h"

#include "raylib.h"
#include "screens/tuner.h"
#include "ui/hitfeedback.h"
#include "ui/menulist.h"
#include "ui/rungraph.h"
#include "ui/theme.h"
#include "ui/ui.h"

#include <algorithm>
#include <cctype>
#include <cmath>

// Immediate mode: these functions run every frame, drawing the widgets and reacting to clicks in the same call.

// The list screens' area: under the title, down to the hints, and the width of the left part of the screen
static MenuListArea listArea(float widthShare){
    float width = ImGui::GetWindowWidth(), height = ImGui::GetWindowHeight(), s = menuScale();
    return { ImVec2(width * 0.07f, height * 0.25f), width * widthShare, height * 0.66f - 20 * s, s };
}

// The selected song on a card beside the list: its parts, and the best run on each
static bool practiceMode = false; // the song list's mode: playing a song, or practising part of it

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
        std::string instrument = part.type == InstrumentType::Keys ? "KEYS, MIDI"
                               : TextFormat("%s, %d STRINGS", part.type == InstrumentType::Bass ? "BASS" : "GUITAR", part.stringCount);
        draw->AddText(fonts.mono, 13 * s, ImVec2(x, y), uiColor(UiColor::Dim), TextFormat("%s  ·  %s", part.name.c_str(), instrument.c_str()));
        float rowY = y + 20 * s;
        bool played = part.played;
        if (!played){
            draw->AddText(fonts.text, 18 * s, ImVec2(x, rowY + 4 * s), uiColor(UiColor::Dim), "Not played yet");
        } else {
            const RunRecord& best = part.best;
            draw->AddText(fonts.heavy, 30 * s, ImVec2(x, rowY - 2 * s), uiColor(gradeColor(best.grade())), gradeName(best.grade()));
            draw->AddText(fonts.bold, 20 * s, ImVec2(x + 56 * s, rowY + 4 * s), uiColor(UiColor::Ink),
                          TextFormat("%.2f%%   %lld", best.accuracy, best.score));
            if (best.fullCombo()){
                draw->AddText(fonts.mono, 13 * s, ImVec2(x + inner - 30 * s, rowY + 9 * s), uiColor(UiColor::Accent), "FC");
            }
            // How the runs went, oldest first: a small line of their scores
            const std::vector<RunRecord>& history = part.history;
            if (history.size() >= 2){
                RunGraphLook small;
                small.labels = false;
                drawRunHistory(draw, history, ImVec2(x + 236 * s, rowY), ImVec2(inner - 236 * s - 48 * s, 28 * s), s, small);
            }
        }
        y += partHeight;
    }
}

static int choosingPartOf = -1; // the song whose parts are listed, -1 when the songs are
static int deleting = -1;       // the song about to be deleted, asked about first; -1 for none
static bool partsJustOpened = false; // its first frame: the selection starts on the first part that can be played

bool songSelectBack(){
    if (choosingPartOf < 0 && deleting < 0) return false;
    choosingPartOf = -1;
    deleting = -1;
    return true;
}

static const char* instrumentName(InstrumentType type){
    switch (type){
        case InstrumentType::Bass: return "bass";
        case InstrumentType::Keys: return "keys";
        default: return "guitar";
    }
}

static bool sameWords(const std::string& a, const std::string& b){
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); i++) if (std::tolower((unsigned char)a[i]) != std::tolower((unsigned char)b[i])) return false;
    return true;
}

// The song's parts by instrument, one level down from the songs: "Bass  4 strings", "Guitar  Melody · 6 strings",
// "Keys". Each is played on its own instrument, so one that isn't connected is greyed, with why.
static void partList(const SongEntry& song, SongSelectChoice& choice, const InstrumentStatus& guitar, const InstrumentStatus& bass){
    static MenuList list;
    float s = menuScale();
    menuScreenTitle(song.title.c_str(), s);
    ImGui::GetWindowDrawList()->AddText(uiFonts().text, 18 * s, ImVec2(ImGui::GetWindowWidth() * 0.07f, ImGui::GetWindowHeight() * 0.09f + 52 * s),
                                        uiColor(UiColor::Dim), practiceMode ? "Practice: choose your instrument" : "Choose your instrument");
    if (partsJustOpened){
        // The first one that can be played
        partsJustOpened = false;
        list.selected = 0;
        for (int i = (int)song.parts.size() - 1; i >= 0; i--){
            const SongPart& part = song.parts[i];
            const InstrumentStatus* status = part.type == InstrumentType::Bass ? &bass : part.type == InstrumentType::Guitar ? &guitar : nullptr;
            if (!status || status->ready) list.selected = i;
        }
    }
    std::vector<MenuRow> rows;
    for (const SongPart& part : song.parts){
        MenuRow row;
        std::string instrument = instrumentName(part.type);
        instrument[0] = (char)std::toupper((unsigned char)instrument[0]);
        row.label = instrument;
        std::string name = part.name.empty() || sameWords(part.name, instrument) ? "" : part.name + "  ·  ";
        row.detail = part.type == InstrumentType::Keys ? name + "a MIDI keyboard, or the computer's"
                                                       : name + TextFormat("%d strings", part.stringCount);
        const InstrumentStatus* status = part.type == InstrumentType::Bass ? &bass : part.type == InstrumentType::Guitar ? &guitar : nullptr;
        if (status && !status->ready){
            row.disabled = true;
            row.note = status->problem;
        }
        rows.push_back(row);
    }
    int confirmed = menuList(list, rows, listArea(0.45f));
    drawSongCard(song, s);
    if (confirmed >= 0){
        choice.practice = practiceMode;
        choice.songIndex = choosingPartOf;
        choice.part = confirmed;
        choosingPartOf = -1;
    }
    menuScreenHint("Up/Down  choose    Enter  play    Esc  back to songs", s);
}

static int songToSelect = -1; // selectSongInList

void selectSongInList(int songIndex){
    songToSelect = songIndex;
}

SongSelectChoice songSelectScreen(const char* title, const std::vector<SongEntry>& songs, const std::string& error,
                                  const std::string& notice, bool forEditing, const InstrumentStatus& guitar,
                                  const InstrumentStatus& bass){
    static MenuList list; // one for playing and editing: switching keeps the song chosen
    if (!forEditing && songToSelect >= 0){
        list.selected = songToSelect; // rows are the songs first, in order
        songToSelect = -1;
    }
    SongSelectChoice choice;
    beginMenu(title);
    float s = menuScale();
    if (deleting >= 0 && deleting < (int)songs.size()){
        // Asked first: a song's chart is hours of someone's work. Keeping it is the first choice.
        static MenuList question;
        static const std::vector<MenuRow> answers = { actionRow("Keep it", "Esc"), actionRow("Delete it") };
        const SongEntry& song = songs[deleting];
        menuScreenTitle(TextFormat("Delete %s?", song.title.c_str()), s);
        ImGui::GetWindowDrawList()->AddText(uiFonts().text, 18 * s, ImVec2(ImGui::GetWindowWidth() * 0.07f, ImGui::GetWindowHeight() * 0.09f + 52 * s),
                                            uiColor(UiColor::Dim), "It goes to the trash folder in your data folder: it can be put back from there. Your scores on it are kept.");
        if (ImGui::IsWindowAppearing() || question.selected < 0) question.selected = 0;
        int answer = menuList(question, answers, listArea(0.45f));
        if (answer == 0) deleting = -1;
        if (answer == 1){
            choice.deleteSong = deleting;
            deleting = -1;
            question.selected = 0;
        }
        menuScreenHint("Up/Down  choose    Enter  confirm    Esc  keep it", s);
        ImGui::End();
        return choice;
    }
    deleting = -1;
    if (!forEditing && choosingPartOf >= 0 && choosingPartOf < (int)songs.size()){
        partList(songs[choosingPartOf], choice, guitar, bass);
        ImGui::End();
        return choice;
    }
    choosingPartOf = -1;
    menuScreenTitle(title, s);
    {
        // Beside the title: playing the song through, practising part of it, or editing it (Tab goes round them)
        float x = ImGui::GetWindowWidth() * 0.55f, y = ImGui::GetWindowHeight() * 0.09f + 14 * s;
        const char* const modes[] = { "PLAY", "PRACTICE", "EDIT" };
        const int was = forEditing ? 2 : practiceMode ? 1 : 0;
        int mode = was;
        if (ImGui::IsKeyPressed(ImGuiKey_Tab)) mode = (mode + 1) % 3;
        menuSwitchRow("MODE", modes, 3, mode, x, y, s);
        if (mode != was){
            if (mode < 2) practiceMode = mode == 1;
            choice.switchEditing = (mode == 2) != forEditing;
        }
    }

    // The songs, then the data folder
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
    if (forEditing) rows.push_back(actionRow("New song from audio or video"));
    const int importSong = (int)rows.size();
    rows.push_back(actionRow("Import a song"));
    const int openFolder = (int)rows.size();
    rows.push_back(actionRow("Open data folder"));

    int confirmed = menuList(list, rows, listArea(0.45f));
    if (list.selected >= 0 && list.selected < (int)songs.size() && songs[list.selected].error.empty()) drawSongCard(songs[list.selected], s);
    if (confirmed >= 0 && confirmed < (int)songs.size()){
        // Playing: the instrument to play it with first
        if (!forEditing && !songs[confirmed].parts.empty()){
            choosingPartOf = confirmed;
            partsJustOpened = true;
            choice.partsOpened = true;
        } else {
            choice.songIndex = confirmed;
            choice.practice = practiceMode;
        }
    }
    if (confirmed == newSong) choice.newSong = true;
    if (confirmed == openFolder) choice.openDataFolder = true;
    if (confirmed == importSong) choice.importSong = true;
    // The player's own songs can be deleted (Delete, or the button): the ones that come with lahn can't
    if (list.selected >= 0 && list.selected < (int)songs.size() && !songs[list.selected].builtIn){
        bool pressed = menuPill("Delete", "Del", ImVec2(ImGui::GetWindowWidth() * 0.93f, ImGui::GetWindowHeight() - 48 * s), true, 0, s);
        if (pressed || ImGui::IsKeyPressed(ImGuiKey_Delete)) deleting = list.selected;
    }

    if (!error.empty() || !notice.empty()){
        ImGui::SetCursorPos(ImVec2(ImGui::GetWindowWidth() * 0.07f, ImGui::GetWindowHeight() * 0.17f + 20 * s));
        if (!error.empty()) ImGui::TextColored(uiColorVec(UiColor::Bad), "%s", error.c_str());
        else ImGui::TextColored(uiColorVec(UiColor::Good), "%s", notice.c_str());
    }
    menuScreenHint(forEditing ? "Up/Down  choose    Enter  edit    Tab  mode    Drop audio, a video, a tab or a .lahn to add a song    Esc  back"
                              : "Up/Down  choose    Enter  choose    Tab  mode    Drop a tab or a song to import it    Esc  back", s);
    ImGui::End();
    return choice;
}

PauseChoice pauseScreen(const std::string& song, bool practising, const char* instrument, bool canSwitch){
    static MenuList list;
    // Resume, start over, switch between playing it through and practising part of it, tune, leave
    std::vector<MenuRow> rows = { actionRow("Resume", "Esc"), actionRow(practising ? "Start the practice over" : "Retry") };
    std::vector<PauseChoice> choices = { PauseChoice::Resume, PauseChoice::Retry };
    if (practising){
        rows.push_back(actionRow("Practice settings"));
        choices.push_back(PauseChoice::PracticeSettings);
    }
    if (canSwitch){
        rows.push_back(actionRow(practising ? "Quit practice mode" : "Practise this part")); // quitting it, the song is played through
        choices.push_back(PauseChoice::SwitchMode);
    }
    if (instrument){
        rows.push_back(actionRow(TextFormat("Tune your %s", instrument)));
        choices.push_back(PauseChoice::Tune);
    }
    rows.push_back(actionRow("Quit to songs"));
    choices.push_back(PauseChoice::Quit);
    PauseChoice choice = PauseChoice::None;
    beginMenu("Paused");
    float s = menuScale(), width = ImGui::GetWindowWidth(), height = ImGui::GetWindowHeight();
    if (ImGui::IsWindowAppearing()) list.selected = 0; // Resume first, every time
    // The play screen stays in sight, dimmed behind the menu
    ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(0, 0), ImVec2(width, height), uiColor(UiColor::Background, 0.88f));
    menuScreenTitle(practising ? "Paused  ·  practice" : "Paused", s);
    ImGui::GetWindowDrawList()->AddText(uiFonts().text, 18 * s, ImVec2(width * 0.07f, height * 0.09f + 50 * s), uiColor(UiColor::Dim), song.c_str());
    int confirmed = menuList(list, rows, {ImVec2(width * 0.07f, height * 0.25f), width * 0.45f, rows.size() * 48 * s, s});
    if (confirmed >= 0 && confirmed < (int)choices.size()) choice = choices[confirmed];
    menuScreenHint("Enter  choose    Esc  resume", s);
    ImGui::End();
    return choice;
}

OutOfTuneChoice outOfTuneScreen(const std::string& song, const char* instrument, float cents){
    static MenuList list;
    static const std::vector<MenuRow> rows = { actionRow("Tune and start over"), actionRow("Play on", "Esc"), actionRow("Quit to songs") };
    OutOfTuneChoice choice = OutOfTuneChoice::None;
    beginMenu("Out of tune");
    float s = menuScale(), width = ImGui::GetWindowWidth(), height = ImGui::GetWindowHeight();
    if (ImGui::IsWindowAppearing()) list.selected = 0; // tuning first: it's why the song stopped
    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(ImVec2(0, 0), ImVec2(width, height), uiColor(UiColor::Background, 0.9f));
    menuScreenTitle(TextFormat("Your %s is out of tune", instrument), s);
    draw->AddText(uiFonts().text, 18 * s, ImVec2(width * 0.07f, height * 0.09f + 52 * s), uiColor(UiColor::Accent),
                  TextFormat("Its notes keep coming out about %.0f cents %s. Tuning it takes a moment.", std::fabs(cents),
                             cents > 0 ? "sharp (high)" : "flat (low)"));
    draw->AddText(uiFonts().text, 18 * s, ImVec2(width * 0.07f, height * 0.09f + 76 * s), uiColor(UiColor::Dim), song.c_str());
    int confirmed = menuList(list, rows, {ImVec2(width * 0.07f, height * 0.3f), width * 0.45f, 3 * 48 * s, s});
    if (confirmed == 0) choice = OutOfTuneChoice::Retune;
    if (confirmed == 1) choice = OutOfTuneChoice::PlayOn;
    if (confirmed == 2) choice = OutOfTuneChoice::Quit;
    menuScreenHint("Enter  choose    Esc  play on", s);
    ImGui::End();
    return choice;
}

// Looking at the run note by note (What you played): Esc goes back to the rest of the results
static bool comparingNotes = false;

bool resultsBack(){
    if (!comparingNotes) return false;
    comparingNotes = false;
    return true;
}

ResultsChoice resultsScreen(const GameResult& result){
    static MenuList list;
    static const std::vector<MenuRow> rows = { actionRow("Retry"), actionRow("What you played"), actionRow("Back to songs", "Esc") };
    ResultsChoice choice = ResultsChoice::None;
    Grade grade = gradeFor(result.accuracy, result.missCount);

    beginMenu("Results");
    float s = menuScale(), width = ImGui::GetWindowWidth(), height = ImGui::GetWindowHeight();
    std::string title = result.title + (result.partName.empty() ? "" : "  ·  " + result.partName);
    menuScreenTitle(title.c_str(), s);
    if (ImGui::IsWindowAppearing()){
        list.selected = 0; // Retry first, every time
        comparingNotes = false;
        resetNoteComparison();
    }

    ImDrawList* draw = ImGui::GetWindowDrawList();
    const UiFonts& fonts = uiFonts();
    if (comparingNotes){
        // The run note by note: what was written against what was played
        int right = 0, missed = 0, wrong = 0;
        for (const WrittenNote& note : result.written) (note.hit ? right : missed)++;
        for (const HeardPitch& heard : result.heard){
            bool matched = false;
            for (const WrittenNote& note : result.written){
                if (note.pitch == heard.pitch && heard.time > note.time - 0.15f && heard.time < note.time + std::max(note.length, 0.15f)){ matched = true; break; }
            }
            wrong += !matched;
        }
        const float left = width * 0.07f, top = height * 0.17f;
        draw->AddText(fonts.mono, 14 * s, ImVec2(left, top), uiColor(UiColor::Dim),
                      TextFormat("%d NOTES WRITTEN  ·  %d PLAYED  ·  %d MISSED  ·  %d WRONG NOTES", (int)result.written.size(), right, missed, wrong));
        const float legendY = top + 22 * s;
        float lx = left;
        auto legend = [&](ImU32 color, bool dot, const char* text){
            if (dot) draw->AddCircleFilled(ImVec2(lx + 5 * s, legendY + 8 * s), 5 * s, color);
            else draw->AddRect(ImVec2(lx, legendY + 3 * s), ImVec2(lx + 22 * s, legendY + 13 * s), color, 3 * s, 0, 2 * s);
            lx += (dot ? 16 : 30) * s;
            draw->AddText(fonts.text, 15 * s, ImVec2(lx, legendY), uiColor(UiColor::Dim), text);
            lx += (fonts.text ? fonts.text->CalcTextSizeA(15 * s, FLT_MAX, 0.0f, text).x : 100 * s) + 24 * s;
        };
        legend(uiColor(UiColor::Good), false, "written, played perfect");
        legend(uiColor(UiColor::Accent), false, "played");
        legend(uiColor(UiColor::Bad), false, "missed");
        legend(uiColor(UiColor::Good), true, "you played it");
        legend(uiColor(UiColor::Bad), true, "a wrong note");
        if (result.heard.empty() && !result.withInstrument){
            draw->AddText(fonts.text, 15 * s, ImVec2(left, legendY + 22 * s), uiColor(UiColor::Dim), "Played on the computer keyboard: there are no pitches to show, only which notes were hit.");
        }
        drawNoteComparison(result, ImVec2(left, top + 70 * s), ImVec2(width * 0.93f, height - 64 * s), s);
        menuScreenHint("Wheel, drag or Left/Right  along the song    Esc  back to the results", s);
        ImGui::End();
        return choice;
    }
    auto textWidth = [](ImFont* font, float size, const char* text){ return font ? font->CalcTextSizeA(size, FLT_MAX, 0.0f, text).x : size * 3; };

    // The run, on a card: the grade big, the accuracy by it, then the score, the combo and the timing
    ImVec2 card(width * 0.55f, height * 0.25f);
    float cardWidth = width * 0.38f, pad = 26 * s, inner = cardWidth - 2 * pad;
    const float distributionHeight = 64 * s;
    float cardHeight = pad * 2 + 110 * s + 3 * (13 * s + 8 * s + 30 * s + 14 * s) + distributionHeight + 32 * s + 22 * s;
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
    row("SCORE", TextFormat("%lld", result.score), UiColor::Ink, "");
    bool fullCombo = result.missCount == 0 && result.totalNotes > 0;
    row("BEST COMBO", TextFormat("%d", result.maxCombo), fullCombo ? UiColor::Accent : UiColor::Ink,
        fullCombo ? "FULL COMBO" : TextFormat("of %d notes", result.totalNotes));
    const TimingStats& timing = result.timing;
    std::string lean = std::fabs(timing.meanMs) < 1.0f ? "right on average"
                     : TextFormat("%.0f ms %s on average", std::fabs(timing.meanMs), timing.meanMs > 0 ? "early" : "late");
    row("TIMING", TextFormat("%.0f UR", timing.unstableRate), UiColor::Ink, lean);
    // The run's timing distribution: it arrives from the play screen, where it was, growing into its place here
    static double shownAt = 0.0;
    if (ImGui::IsWindowAppearing()) shownAt = GetTime();
    float flight = std::clamp((float)(GetTime() - shownAt) / 0.7f, 0.0f, 1.0f);
    flight = 1.0f - (1.0f - flight) * (1.0f - flight) * (1.0f - flight); // fast, then settling
    const Rectangle& from = result.distributionFrom;
    bool fromPlay = from.width > 0.0f;
    ImVec2 at(x, y + 4 * s), size(inner, distributionHeight);
    if (fromPlay){
        at = ImVec2(from.x + (at.x - from.x) * flight, from.y + (at.y - from.y) * flight);
        size = ImVec2(from.width + (size.x - from.width) * flight, from.height + (size.y - from.height) * flight);
    }
    // Once it's there, the bars give way to the curve, traced from the left, then the area under it fills in
    float since = (float)(GetTime() - shownAt);
    DistributionLook look;
    look.curve = std::clamp((since - 0.8f) / 0.9f, 0.0f, 1.0f);
    look.bars = 1.0f - std::clamp((since - 0.7f) / 0.5f, 0.0f, 1.0f);
    look.fill = std::clamp((since - 1.7f) / 0.5f, 0.0f, 1.0f);
    drawTimingDistribution(ImGui::GetForegroundDrawList(), result.errorsMs, at, size, s, look);
    y += distributionHeight + 32 * s;
    draw->AddText(fonts.text, 17 * s, ImVec2(x, y), uiColor(UiColor::Dim),
                  TextFormat("Perfect %d  ·  Good %d  ·  Miss %d%s", result.perfectCount, result.nearCount, result.missCount,
                             result.withInstrument ? "" : "  ·  keyboard"));

    // Retry and Back, and under them the part's best runs, this one highlighted
    float left = width * 0.07f, listTop = height * 0.25f;
    int confirmed = menuList(list, rows, {ImVec2(left, listTop), width * 0.4f, 3 * 48 * s, s});
    if (confirmed == 0) choice = ResultsChoice::Retry;
    if (confirmed == 1) comparingNotes = true;
    if (confirmed == 2) choice = ResultsChoice::BackToSongs;
    if (!result.records.empty()){
        float boardY = listTop + 3 * 48 * s + 36 * s;
        draw->AddText(fonts.mono, 13 * s, ImVec2(left, boardY), uiColor(UiColor::Dim), "YOUR BEST RUNS");
        boardY += 13 * s + 12 * s;
        for (int i = 0; i < (int)result.records.size() && i < 5; i++){
            const RunRecord& run = result.records[i];
            bool thisRun = i == result.place;
            ImU32 ink = uiColor(thisRun ? UiColor::Accent : UiColor::Ink), dim = uiColor(thisRun ? UiColor::Accent : UiColor::Dim);
            draw->AddText(fonts.mono, 15 * s, ImVec2(left, boardY + 3 * s), dim, TextFormat("%d", i + 1));
            draw->AddText(fonts.heavy, 20 * s, ImVec2(left + 30 * s, boardY), uiColor(thisRun ? UiColor::Accent : gradeColor(run.grade())), gradeName(run.grade()));
            draw->AddText(fonts.bold, 20 * s, ImVec2(left + 72 * s, boardY), ink, TextFormat("%lld", run.score));
            draw->AddText(fonts.text, 17 * s, ImVec2(left + 180 * s, boardY + 2 * s), dim,
                          TextFormat("%.2f%%  ·  %s%s  ·  %s", run.accuracy, run.fullCombo() ? "FC" : TextFormat("%dx", run.maxCombo),
                                     run.withInstrument ? "" : "  ·  keys", run.date.c_str()));
            boardY += 30 * s;
        }
        // Every run of the part as it was played, this one marked: traced in once the card has arrived
        float graphTop = boardY + 22 * s + 13 * s + 26 * s, graphBottom = height - 64 * s - 18 * s;
        if (result.history.size() >= 2 && graphBottom - graphTop > 40 * s){
            draw->AddText(fonts.mono, 13 * s, ImVec2(left, boardY + 22 * s), uiColor(UiColor::Dim),
                          TextFormat("PROGRESS  ·  %d RUNS", (int)result.history.size()));
            RunGraphLook graph;
            graph.marked = (int)result.history.size() - 1;
            graph.reveal = std::clamp((since - 0.9f) / 0.8f, 0.0f, 1.0f);
            graph.reveal = 1.0f - (1.0f - graph.reveal) * (1.0f - graph.reveal);
            drawRunHistory(draw, result.history, ImVec2(left, graphTop), ImVec2(width * 0.4f, graphBottom - graphTop), s, graph);
        }
    }
    menuScreenHint("Enter  choose    Esc  back to songs", s);
    ImGui::End();
    return choice;
}

void tunerScreen(){
    beginMenu("Tuner");
    menuTitle("Tuner");
    drawTuner();
    ImGui::End();
}
