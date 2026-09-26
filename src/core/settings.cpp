#include "core/settings.h"

#include "core/files.h"

#include <algorithm>
#include <fstream>
#include <sstream>

const int SUPPORTED_SETTINGS_VERSION = 1;

// Stored as words rather than numbers, so the file stays readable and reordering the enum can't break it
const char* const NOTE_VIEW_NAMES[] = { "highway", "staff", "both" };

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
        else if (key == "master_volume") number(settings.masterVolume, 0.0f, 1.0f);
        else if (key == "preview_volume") number(settings.previewVolume, 0.0f, 1.0f);
        else if (key == "preview_sound"){ if (!value.empty()) settings.previewSound = value; }
        else if (key == "note_view"){
            bool known = false;
            for (int i = 0; i < 3; i++) if (value == NOTE_VIEW_NAMES[i]){ settings.noteView = (NoteView)i; known = true; }
            if (!known) warnings.push_back("line " + std::to_string(lineNumber) + ": unknown note view '" + value + "', keeping default");
        }
        else if (key == "low_string_on_top") settings.lowStringOnTop = value == "1";
        else if (key == "fullscreen") settings.fullscreen = value == "1";
        else if (key == "frame_rate_limit") number(settings.frameRateLimit, 0, 1000);
        else if (key == "note_speed") number(settings.noteSpeed, 100.0f, 1500.0f);
        else if (key == "global_offset_ms") number(settings.globalOffsetMs, -500, 500);
        else warnings.push_back("line " + std::to_string(lineNumber) + ": unknown setting '" + key + "', ignored");
    }
    return settings;
}

bool saveSettings(const std::string& path, const Settings& settings, std::string& error){
    std::ostringstream out;
    out << "# OpenMusicTrainer settings\n";
    out << "version " << SUPPORTED_SETTINGS_VERSION << "\n\n";
    out << "output_device " << settings.outputDevice << "\n";
    out << "input_device " << settings.inputDevice << "\n";
    out << "master_volume " << settings.masterVolume << "\n";
    out << "preview_volume " << settings.previewVolume << "\n";
    out << "preview_sound " << settings.previewSound << "\n\n";
    out << "note_view " << NOTE_VIEW_NAMES[(int)settings.noteView] << "\n";
    out << "low_string_on_top " << (settings.lowStringOnTop ? 1 : 0) << "\n";
    out << "fullscreen " << (settings.fullscreen ? 1 : 0) << "\n";
    out << "frame_rate_limit " << settings.frameRateLimit << "\n\n";
    out << "note_speed " << settings.noteSpeed << "\n";
    out << "global_offset_ms " << settings.globalOffsetMs << "\n";
    return writeFileAtomically(path, out.str(), error);
}
