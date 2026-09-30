#include "core/settings.h"

#include "core/files.h"

#include <algorithm>
#include <fstream>
#include <sstream>

const int SUPPORTED_SETTINGS_VERSION = 1;

// The note views are stored as a list of words, "note_view staff tab highway neck", so the file stays readable.
// "both" is how version 1 files first wrote sheet music plus the highway. Only the three switches are read: the
// highway's direction is a line of its own.
static bool readNoteViews(const std::string& value, NoteViews& views){
    NoteViews read;
    read.highway = false;
    std::istringstream words(value);
    std::string word;
    while (words >> word){
        if (word == "staff") read.staff = true;
        else if (word == "tab") read.tab = true;
        else if (word == "highway") read.highway = true;
        else if (word == "neck") read.neck = true;
        else if (word == "both") read.staff = read.highway = true;
        else return false;
    }
    if (!read.any()) return false;
    views.staff = read.staff;
    views.tab = read.tab;
    views.highway = read.highway;
    views.neck = read.neck;
    return true;
}

Settings loadSettings(const std::string& path, std::vector<std::string>& warnings){
    Settings settings;
    std::ifstream file(path);
    if (!file) return settings;

    std::string line;
    int lineNumber = 0;
    while (std::getline(file, line)){
        lineNumber++;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        std::istringstream ss(line);
        std::string key;
        if (!(ss >> key) || key[0] == '#') continue;
        std::string value;
        std::getline(ss >> std::ws, value); // the rest of the line: device names contain spaces

        // Reads a number; a bad one is reported and the default is kept
        auto number = [&](auto& target, auto low, auto high){
            std::istringstream valueStream(value);
            auto parsed = target;
            if (valueStream >> parsed) target = std::clamp(parsed, (decltype(parsed))low, (decltype(parsed))high);
            else warnings.push_back("line " + std::to_string(lineNumber) + ": '" + key + "' needs a number, keeping default");
        };

        if (key == "version") continue; // only one version so far; later versions may need converting here
        else if (key == "output_device") settings.outputDevice = value;
        else if (key == "input_device") settings.inputDevice = value;
        else if (key == "midi_device") settings.midiDevice = value;
        else if (key == "exclusive_input") settings.exclusiveInput = value == "1";
        else if (key == "guitar_input" || key == "bass_input" || key == "voice_input"){
            // "all", or an input's number from 1, as the interface prints it
            int& channel = key == "guitar_input" ? settings.guitarChannel : key == "bass_input" ? settings.bassChannel : settings.voiceChannel;
            int input = channel + 1;
            if (value == "all") channel = -1;
            else {
                number(input, 1, 64);
                channel = input - 1; // unchanged if it didn't read as a number
            }
        }
        else if (key == "piano_keys"){
            std::istringstream names(value);
            std::vector<std::string> keys;
            std::string name;
            while (names >> name) keys.push_back(name);
            if ((int)keys.size() == PIANO_KEY_SLOTS) settings.pianoKeys = keys;
            else warnings.push_back("line " + std::to_string(lineNumber) + ": piano_keys needs " + std::to_string(PIANO_KEY_SLOTS) + " keys, keeping the default");
        }
        else if (key == "master_volume") number(settings.masterVolume, 0.0f, 1.0f);
        else if (key == "preview_volume") number(settings.previewVolume, 0.0f, 1.0f);
        else if (key == "hit_sound_volume") number(settings.hitSoundVolume, 0.0f, 1.0f);
        else if (key == "monitor") settings.monitorOn = value == "1";
        else if (key == "monitor_sound"){
            if (value == "synth" || value == "instrument") settings.monitorSynth = value == "synth";
            else warnings.push_back("line " + std::to_string(lineNumber) + ": monitor_sound is synth or instrument, keeping default");
        }
        else if (key == "monitor_volume") number(settings.monitorVolume, 0.0f, 1.0f);
        else if (key == "monitor_drive") number(settings.monitorDrive, 0.0f, 1.0f);
        else if (key == "monitor_tone") number(settings.monitorTone, 0.0f, 1.0f);
        else if (key == "preview_sound"){ if (!value.empty()) settings.previewSound = value; }
        else if (key == "note_view"){
            if (!readNoteViews(value, settings.noteViews)) warnings.push_back("line " + std::to_string(lineNumber) + ": unknown note view '" + value + "', keeping default");
        }
        else if (key == "highway_direction"){
            if (value == "across") settings.noteViews.highwayFalls = false;
            else if (value == "falling") settings.noteViews.highwayFalls = true;
            else warnings.push_back("line " + std::to_string(lineNumber) + ": highway_direction is across or falling, keeping default");
        }
        else if (key == "low_string_on_top") settings.lowStringOnTop = value == "1";
        else if (key == "fullscreen") settings.fullscreen = value == "1";
        else if (key == "theme"){
            if (value == "light" || value == "dark") settings.darkTheme = value == "dark";
            else warnings.push_back("line " + std::to_string(lineNumber) + ": theme is light or dark, keeping default");
        }
        else if (key == "frame_rate_limit") number(settings.frameRateLimit, 0, 1000);
        else if (key == "play_with_instrument") settings.playWithInstrument = value == "1";
        else if (key == "note_speed") number(settings.noteSpeed, 100.0f, 1500.0f);
        else if (key == "global_offset_ms") number(settings.globalOffsetMs, -500, 500);
        else if (key == "input_offset_ms") number(settings.inputOffsetMs, -500, 500);
        else warnings.push_back("line " + std::to_string(lineNumber) + ": unknown setting '" + key + "', ignored");
    }
    return settings;
}

bool saveSettings(const std::string& path, const Settings& settings, std::string& error){
    std::ostringstream out;
    out << "# lahn settings\n";
    out << "version " << SUPPORTED_SETTINGS_VERSION << "\n\n";
    out << "output_device " << settings.outputDevice << "\n";
    out << "input_device " << settings.inputDevice << "\n";
    out << "midi_device " << settings.midiDevice << "\n";
    auto input = [](int channel){ return channel < 0 ? std::string("all") : std::to_string(channel + 1); };
    out << "guitar_input " << input(settings.guitarChannel) << "\n";
    out << "bass_input " << input(settings.bassChannel) << "\n";
    out << "voice_input " << input(settings.voiceChannel) << "\n";
    out << "exclusive_input " << (settings.exclusiveInput ? 1 : 0) << "\n";
    out << "master_volume " << settings.masterVolume << "\n";
    out << "preview_volume " << settings.previewVolume << "\n";
    out << "hit_sound_volume " << settings.hitSoundVolume << "\n";
    out << "monitor " << (settings.monitorOn ? 1 : 0) << "\n";
    out << "monitor_sound " << (settings.monitorSynth ? "synth" : "instrument") << "\n";
    out << "monitor_volume " << settings.monitorVolume << "\n";
    out << "monitor_drive " << settings.monitorDrive << "\n";
    out << "monitor_tone " << settings.monitorTone << "\n";
    out << "preview_sound " << settings.previewSound << "\n\n";
    out << "note_view";
    if (settings.noteViews.staff) out << " staff";
    if (settings.noteViews.tab) out << " tab";
    if (settings.noteViews.highway) out << " highway";
    if (settings.noteViews.neck) out << " neck";
    out << "\n";
    out << "highway_direction " << (settings.noteViews.highwayFalls ? "falling" : "across") << "\n";
    out << "low_string_on_top " << (settings.lowStringOnTop ? 1 : 0) << "\n";
    out << "theme " << (settings.darkTheme ? "dark" : "light") << "\n";
    out << "fullscreen " << (settings.fullscreen ? 1 : 0) << "\n";
    out << "frame_rate_limit " << settings.frameRateLimit << "\n\n";
    out << "play_with_instrument " << (settings.playWithInstrument ? 1 : 0) << "\n";
    out << "note_speed " << settings.noteSpeed << "\n";
    out << "global_offset_ms " << settings.globalOffsetMs << "\n";
    out << "piano_keys";
    for (const std::string& key : settings.pianoKeys) out << " " << key;
    out << "\n";
    out << "input_offset_ms " << settings.inputOffsetMs << "\n";
    return writeFileAtomically(path, out.str(), error);
}

int channelFor(const Settings& settings, InputRole role){
    switch (role){
        case InputRole::Guitar: return settings.guitarChannel;
        case InputRole::Bass:   return settings.bassChannel;
        case InputRole::Voice:  return settings.voiceChannel;
    }
    return -1;
}
