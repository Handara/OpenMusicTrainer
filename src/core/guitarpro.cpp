#include "core/guitarpro.h"

#include "core/xml.h"
#include "miniz.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <map>
#include <set>
#include <sstream>

const int RESOLUTION = 480;       // ticks per quarter note: a triplet sixteenth is a whole number of them
const int MAX_PLAYED_BARS = 20000; // repeats played out, at most: a file whose repeats never end stops here
const int BASS_BELOW = 36;        // a track whose lowest string is under C2 is a bass (its name aside)

namespace {

std::vector<int> numbers(const std::string& text){
    std::vector<int> values;
    std::istringstream in(text);
    int value;
    while (in >> value) values.push_back(value);
    return values;
}

// The elements of one kind, by their id attribute: <Beats><Beat id="3">...
std::map<int, const XmlNode*> byId(const XmlNode& root, const char* list, const char* item){
    std::map<int, const XmlNode*> found;
    if (const XmlNode* parent = root.child(list)){
        for (const XmlNode* node : parent->childrenNamed(item)) found[std::atoi(node->attribute("id").c_str())] = node;
    }
    return found;
}

// A <Property name="..."> among a node's <Properties>
const XmlNode* property(const XmlNode& node, const char* name){
    const XmlNode* properties = node.child("Properties");
    if (!properties) return nullptr;
    for (const XmlNode* p : properties->childrenNamed("Property")) if (p->attribute("name") == name) return p;
    return nullptr;
}

// How long a rhythm lasts, in ticks: its value, dotted, in a tuplet
int rhythmTicks(const XmlNode* rhythm){
    if (!rhythm) return RESOLUTION;
    static const std::pair<const char*, double> VALUES[] = {
        { "DoubleWhole", 8.0 }, { "Whole", 4.0 }, { "Half", 2.0 }, { "Quarter", 1.0 }, { "Eighth", 0.5 },
        { "16th", 0.25 }, { "32nd", 0.125 }, { "64th", 0.0625 }, { "128th", 0.03125 },
    };
    double quarters = 1.0;
    std::string value = rhythm->childText("NoteValue");
    for (const auto& [name, length] : VALUES) if (value == name) quarters = length;
    if (const XmlNode* dot = rhythm->child("AugmentationDot")){
        int dots = std::atoi(dot->attribute("count").c_str());
        if (dots == 1) quarters *= 1.5;
        if (dots >= 2) quarters *= 1.75;
    }
    if (const XmlNode* tuplet = rhythm->child("PrimaryTuplet")){
        int num = std::atoi(tuplet->attribute("num").c_str()), den = std::atoi(tuplet->attribute("den").c_str());
        if (num > 0 && den > 0) quarters = quarters * den / num;
    }
    return std::max(1, (int)std::lround(quarters * RESOLUTION));
}

struct GpTrack {
    std::string name;
    std::vector<int> pitches; // by the file's string numbers
    std::string instrument;   // what the file calls it, lowercase: "electricbass" (7 and 8), "e-bass4" (6), "" if it doesn't say
};

struct MasterBar {
    int beats = 4, beatUnit = 4;
    int fifths = 0;
    bool minor = false;
    std::vector<int> bars;    // one per track, in the tracks' order
    bool repeatStart = false, repeatEnd = false;
    int repeatCount = 2;      // how many times the repeated part plays
    std::vector<int> endings; // alternate endings: the passes this bar plays on (empty: every pass)
};

// The order the bars are played in, repeats and alternate endings played out
std::vector<int> playOrder(const std::vector<MasterBar>& masterBars){
    std::vector<int> order;
    int start = 0, pass = 1;
    size_t i = 0;
    bool jumped = false;
    while (i < masterBars.size() && (int)order.size() < MAX_PLAYED_BARS){
        const MasterBar& bar = masterBars[i];
        if (bar.repeatStart && !jumped){
            start = (int)i;
            pass = 1;
        }
        jumped = false;
        bool plays = bar.endings.empty() || std::count(bar.endings.begin(), bar.endings.end(), pass) > 0;
        if (plays) order.push_back((int)i);
        if (plays && bar.repeatEnd && pass < std::max(2, bar.repeatCount)){
            pass++;
            i = start;
            jumped = true;
            continue;
        }
        if (bar.repeatEnd && plays) pass = 1;
        i++;
    }
    return order;
}

// The instrument a track is for, as the file names it: Guitar Pro 7 and 8 say "electricGuitar", "steelGuitar",
// "electricBass", "violin", "drumKit"; Guitar Pro 6 "e-gtr6", "s-gtr6", "e-bass4", "vln", "drmkt"
std::string instrumentOf(const XmlNode& track){
    std::string kind;
    if (const XmlNode* set = track.child("InstrumentSet")) kind = set->childText("Type");
    if (kind.empty()) if (const XmlNode* instrument = track.child("Instrument")) kind = instrument->attribute("ref");
    std::transform(kind.begin(), kind.end(), kind.begin(), [](unsigned char c){ return (char)std::tolower(c); });
    return kind;
}

bool contains(const std::string& text, const char* part){ return text.find(part) != std::string::npos; }

} // namespace

bool readGpif(const std::string& xml, GuitarProImport& out, std::string& error){
    XmlNode root;
    if (!parseXml(xml, root, error)){
        error = "the score can't be read (" + error + ")";
        return false;
    }
    if (root.name != "GPIF"){
        error = "not a Guitar Pro score";
        return false;
    }
    out = GuitarProImport{};
    Chart& chart = out.chart;
    chart.version = 2;
    chart.resolution = RESOLUTION;
    chart.offset = 0.0;
    if (const XmlNode* score = root.child("Score")){
        chart.title = score->childText("Title");
        chart.artist = score->childText("Artist");
    }

    // The tracks, in the order the master bars list their bars
    std::map<int, const XmlNode*> trackNodes = byId(root, "Tracks", "Track");
    std::vector<int> trackOrder;
    if (const XmlNode* master = root.child("MasterTrack")) trackOrder = numbers(master->childText("Tracks"));
    if (trackOrder.empty()) for (const auto& [id, node] : trackNodes) trackOrder.push_back(id);
    std::vector<GpTrack> tracks;
    for (int id : trackOrder){
        GpTrack track;
        auto found = trackNodes.find(id);
        if (found == trackNodes.end()){ tracks.push_back(track); continue; }
        const XmlNode& node = *found->second;
        track.name = node.childText("Name");
        // The tuning: on the track (Guitar Pro 6) or its staff (7 and 8)
        const XmlNode* tuning = property(node, "Tuning");
        if (!tuning){
            if (const XmlNode* staves = node.child("Staves")){
                for (const XmlNode* staff : staves->childrenNamed("Staff")) if (!tuning) tuning = property(*staff, "Tuning");
            }
        }
        if (tuning) track.pitches = numbers(tuning->childText("Pitches"));
        track.instrument = instrumentOf(node);
        tracks.push_back(track);
    }

    // The master bars: their time and key, their repeats, and each track's bar
    std::vector<MasterBar> masterBars;
    if (const XmlNode* list = root.child("MasterBars")){
        for (const XmlNode* node : list->childrenNamed("MasterBar")){
            MasterBar bar;
            std::string time = node->childText("Time");
            size_t slash = time.find('/');
            if (slash != std::string::npos){
                bar.beats = std::max(1, std::atoi(time.substr(0, slash).c_str()));
                bar.beatUnit = std::max(1, std::atoi(time.substr(slash + 1).c_str()));
            }
            if (const XmlNode* key = node->child("Key")){
                bar.fifths = std::clamp(std::atoi(key->childText("AccidentalCount").c_str()), -7, 7);
                bar.minor = key->childText("Mode") == "Minor";
            }
            bar.bars = numbers(node->childText("Bars"));
            if (const XmlNode* repeat = node->child("Repeat")){
                bar.repeatStart = repeat->attribute("start") == "true";
                bar.repeatEnd = repeat->attribute("end") == "true";
                bar.repeatCount = std::max(2, std::atoi(repeat->attribute("count").c_str()));
            }
            bar.endings = numbers(node->childText("AlternateEndings"));
            masterBars.push_back(bar);
        }
    }
    if (masterBars.empty()){
        error = "the score has no bars";
        return false;
    }

    // Tempos, by bar and where in it (a share of the bar); "120 2" is 120 quarter notes a minute
    struct Tempo { int bar; double position; double bpm; };
    std::vector<Tempo> tempos;
    if (const XmlNode* master = root.child("MasterTrack")){
        if (const XmlNode* automations = master->child("Automations")){
            for (const XmlNode* automation : automations->childrenNamed("Automation")){
                if (automation->childText("Type") != "Tempo") continue;
                std::vector<std::string> parts;
                std::istringstream value(automation->childText("Value"));
                std::string word;
                while (value >> word) parts.push_back(word);
                if (parts.empty()) continue;
                double bpm = std::atof(parts[0].c_str());
                if (parts.size() > 1){
                    // The note that beat counts: 1 an eighth, 2 a quarter, 3 a dotted quarter, 4 a half, 5 a dotted half
                    static const double IN_QUARTERS[] = { 1.0, 0.5, 1.0, 1.5, 2.0, 3.0 };
                    int unit = std::atoi(parts[1].c_str());
                    if (unit >= 1 && unit <= 5) bpm *= IN_QUARTERS[unit];
                }
                if (bpm > 0.0) tempos.push_back({ std::atoi(automation->childText("Bar").c_str()), std::atof(automation->childText("Position").c_str()), bpm });
            }
        }
    }

    std::map<int, const XmlNode*> barNodes = byId(root, "Bars", "Bar"), voiceNodes = byId(root, "Voices", "Voice");
    std::map<int, const XmlNode*> beatNodes = byId(root, "Beats", "Beat"), noteNodes = byId(root, "Notes", "Note");
    std::map<int, const XmlNode*> rhythmNodes = byId(root, "Rhythms", "Rhythm");

    // Which tracks come in: guitars and basses, by their instrument (every track has a tuning in a file, a flute's
    // too); a file that doesn't name its instruments, by having strings
    std::vector<int> kept(tracks.size(), -1); // the chart track each file track became
    for (size_t t = 0; t < tracks.size(); t++){
        const GpTrack& track = tracks[t];
        std::string name = track.name.empty() ? "Track " + std::to_string(t + 1) : track.name;
        const std::string& kind = track.instrument;
        bool drums = contains(kind, "drum") || contains(kind, "drmkt") || contains(kind, "percussion");
        bool bassNamed = contains(kind, "bass");
        bool stringed = kind.empty() || bassNamed || contains(kind, "guitar") || contains(kind, "gtr");
        if (drums){ out.leftOut.push_back(name + " (drums)"); continue; }
        if (!stringed || track.pitches.empty() || track.pitches.size() > 7){ out.leftOut.push_back(name + " (not a guitar or a bass)"); continue; }
        FrettedTrack fretted;
        fretted.name = name;
        fretted.tuning = track.pitches;
        std::sort(fretted.tuning.begin(), fretted.tuning.end()); // lowest first, whatever order the file lists them in
        bool bass = bassNamed || (kind.empty() && fretted.tuning.front() < BASS_BELOW);
        fretted.type = bass ? InstrumentType::Bass : InstrumentType::Guitar;
        kept[t] = (int)chart.frettedTracks.size();
        chart.frettedTracks.push_back(fretted);
    }
    if (chart.frettedTracks.empty()){
        error = "no guitar or bass track in it";
        return false;
    }

    // The bars as played, one after the other
    int tick = 0;
    std::set<int> graceBeats, deadNotes; // counted once each, however often a repeat plays them
    int lastBeats = 0, lastUnit = 0, lastFifths = 99;
    bool lastMinor = false;
    for (int index : playOrder(masterBars)){
        const MasterBar& bar = masterBars[index];
        const int length = bar.beats * RESOLUTION * 4 / bar.beatUnit;
        if (bar.beats != lastBeats || bar.beatUnit != lastUnit) chart.timeSignatures.push_back({ tick, bar.beats, bar.beatUnit });
        if (bar.fifths != lastFifths || bar.minor != lastMinor) chart.keys.push_back({ tick, KeySignature{ bar.fifths, bar.minor } });
        lastBeats = bar.beats; lastUnit = bar.beatUnit; lastFifths = bar.fifths; lastMinor = bar.minor;
        for (const Tempo& tempo : tempos){
            if (tempo.bar != index) continue;
            int at = tick + (int)std::lround(std::clamp(tempo.position, 0.0, 1.0) * length);
            if (!chart.tempoMap.empty() && chart.tempoMap.back().tick == at) chart.tempoMap.back().bpm = tempo.bpm;
            else if (chart.tempoMap.empty() || chart.tempoMap.back().bpm != tempo.bpm) chart.tempoMap.push_back({ at, tempo.bpm });
        }

        for (size_t t = 0; t < tracks.size() && t < bar.bars.size(); t++){
            if (kept[t] < 0) continue;
            FrettedTrack& fretted = chart.frettedTracks[kept[t]];
            const GpTrack& track = tracks[t];
            auto barNode = barNodes.find(bar.bars[t]);
            if (barNode == barNodes.end()) continue;
            for (int voiceId : numbers(barNode->second->childText("Voices"))){
                auto voice = voiceNodes.find(voiceId);
                if (voiceId < 0 || voice == voiceNodes.end()) continue;
                int at = tick;
                for (int beatId : numbers(voice->second->childText("Beats"))){
                    auto beat = beatNodes.find(beatId);
                    if (beat == beatNodes.end()) continue;
                    const XmlNode* rhythmRef = beat->second->child("Rhythm");
                    auto rhythm = rhythmRef ? rhythmNodes.find(std::atoi(rhythmRef->attribute("ref").c_str())) : rhythmNodes.end();
                    int duration = rhythmTicks(rhythm == rhythmNodes.end() ? nullptr : rhythm->second);
                    if (beat->second->child("GraceNotes")){ // played before the beat, in no time of its own
                        graceBeats.insert(beatId);
                        continue;
                    }
                    for (int noteId : numbers(beat->second->childText("Notes"))){
                        auto note = noteNodes.find(noteId);
                        if (note == noteNodes.end()) continue;
                        const XmlNode* stringProperty = property(*note->second, "String");
                        const XmlNode* fretProperty = property(*note->second, "Fret");
                        if (!stringProperty || !fretProperty) continue;
                        if (property(*note->second, "Muted")){ deadNotes.insert(noteId); continue; }
                        int string = std::atoi(stringProperty->childText("String").c_str());
                        int fret = std::atoi(fretProperty->childText("Fret").c_str());
                        if (string < 0 || string >= (int)track.pitches.size() || fret < 0 || fret > MAX_FRET) continue;
                        // Its string, counted from the lowest
                        int pitch = track.pitches[string];
                        int stringIndex = (int)(std::find(fretted.tuning.begin(), fretted.tuning.end(), pitch) - fretted.tuning.begin());
                        const XmlNode* tie = note->second->child("Tie");
                        if (tie && tie->attribute("destination") == "true"){
                            // Tied on: the note before on that string rings longer
                            for (auto it = fretted.notes.rbegin(); it != fretted.notes.rend(); ++it){
                                if (it->stringIndex != stringIndex) continue;
                                it->duration = at + duration - it->tick;
                                break;
                            }
                            continue;
                        }
                        fretted.notes.push_back({ at, stringIndex, fret, duration });
                    }
                    at += duration;
                }
            }
        }
        tick += length;
    }
    if (chart.tempoMap.empty() || chart.tempoMap.front().tick != 0) chart.tempoMap.insert(chart.tempoMap.begin(), { 0, 120.0 });
    if (chart.timeSignatures.empty()) chart.timeSignatures.push_back({ 0, 4, 4 });
    if (chart.keys.empty()) chart.keys.push_back({ 0, KeySignature{} });
    chart.endTick = tick;
    for (FrettedTrack& track : chart.frettedTracks){
        std::stable_sort(track.notes.begin(), track.notes.end(), [](const FrettedNote& a, const FrettedNote& b){
            return a.tick != b.tick ? a.tick < b.tick : a.stringIndex < b.stringIndex;
        });
    }
    if (!graceBeats.empty()) out.leftOut.push_back(std::to_string(graceBeats.size()) + (graceBeats.size() == 1 ? " grace note" : " grace notes"));
    if (!deadNotes.empty()) out.leftOut.push_back(std::to_string(deadNotes.size()) + (deadNotes.size() == 1 ? " dead note" : " dead notes"));
    return true;
}

// Guitar Pro 6's compression: a stream of bits, the high bit of each byte first. Each chunk starts with a flag: 1 for
// a copy of bytes already out (a word size in 4 bits, then how far back and how many, each in that many bits, low
// bit first), 0 for bytes as they are (how many in 2 bits, low bit first, then the bytes, 8 bits each).
namespace {
struct Bits {
    const std::string& data;
    size_t position = 8 * 8; // after "BCFZ" and the unpacked length
    bool ended = false;
    int bit(){
        if (position >= data.size() * 8){ ended = true; return 0; }
        int value = ((unsigned char)data[position / 8] >> (7 - position % 8)) & 1;
        position++;
        return value;
    }
    int read(int count){ int value = 0; for (int i = 0; i < count; i++) value = (value << 1) | bit(); return value; }
    int readReversed(int count){ int value = 0; for (int i = 0; i < count; i++) value |= bit() << i; return value; }
};

int integerAt(const std::string& data, size_t at){
    if (at + 4 > data.size()) return 0;
    return (int)((unsigned char)data[at] | ((unsigned char)data[at + 1] << 8) | ((unsigned char)data[at + 2] << 16) | ((unsigned)(unsigned char)data[at + 3] << 24));
}
} // namespace

bool unpackBcfz(const std::string& data, std::string& out, std::string& error){
    if (data.compare(0, 4, "BCFZ") != 0 || data.size() < 8){
        error = "not a compressed Guitar Pro 6 file";
        return false;
    }
    const int expected = integerAt(data, 4);
    if (expected <= 0 || expected > 256 * 1024 * 1024){
        error = "the file is damaged (its size makes no sense)";
        return false;
    }
    out.clear();
    out.reserve(expected);
    Bits bits{ data };
    while ((int)out.size() < expected && !bits.ended){
        if (bits.read(1) == 1){
            int wordSize = bits.read(4);
            int back = bits.readReversed(wordSize), size = bits.readReversed(wordSize);
            if (back <= 0 || back > (int)out.size()){
                error = "the file is damaged (a copy from before its start)";
                return false;
            }
            size_t from = out.size() - back;
            int count = std::min(back, size);
            for (int i = 0; i < count; i++) out += out[from + i];
        } else {
            int size = bits.readReversed(2);
            for (int i = 0; i < size && !bits.ended; i++) out += (char)bits.read(8);
        }
    }
    if ((int)out.size() > expected) out.resize(expected);
    return true;
}

// A "BCFS" file system: 4096-byte sectors, the first left empty; a file's entry is a sector starting with 2, its
// name at 4, its size at 0x8C, and from 0x94 the numbers of the sectors its data is in, up to a 0
bool bcfsFile(const std::string& fileSystem, const std::string& name, std::string& out){
    if (fileSystem.compare(0, 4, "BCFS") != 0) return false;
    const std::string data = fileSystem.substr(4);
    const size_t SECTOR = 0x1000;
    for (size_t at = SECTOR; at + 3 < data.size(); at += SECTOR){
        if (integerAt(data, at) != 2) continue;
        std::string entryName;
        for (size_t i = at + 4; i < at + 4 + 127 && i < data.size() && data[i] != 0; i++) entryName += data[i];
        int size = integerAt(data, at + 0x8C);
        std::string content;
        size_t pointer = at + 0x94, last = at;
        for (int count = 0; count < 100000; count++, pointer += 4){
            int sector = integerAt(data, pointer);
            if (sector <= 0) break;
            last = (size_t)sector * SECTOR;
            if (last >= data.size()) break;
            content += data.substr(last, SECTOR);
        }
        if (entryName == name){
            out = content.substr(0, std::max(0, std::min(size, (int)content.size())));
            return true;
        }
        at = std::max(at, last); // past this file's data, which isn't an entry however it starts
    }
    return false;
}

bool importGuitarPro(const std::string& path, GuitarProImport& out, std::string& error){
    std::ifstream file(path, std::ios::binary);
    if (!file){
        error = "can't open " + path;
        return false;
    }
    std::string data((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    if (data.size() >= 2 && data[0] == 'P' && data[1] == 'K'){
        // Guitar Pro 7 and 8: a zip, the score inside
        mz_zip_archive zip{};
        if (!mz_zip_reader_init_mem(&zip, data.data(), data.size(), 0)){
            error = "the file is damaged (its zip can't be read)";
            return false;
        }
        size_t size = 0;
        void* score = mz_zip_reader_extract_file_to_heap(&zip, "Content/score.gpif", &size, 0);
        mz_zip_reader_end(&zip);
        if (!score){
            error = "no score inside it (Content/score.gpif)";
            return false;
        }
        std::string xml((const char*)score, size);
        mz_free(score);
        return readGpif(xml, out, error);
    }
    if (data.compare(0, 4, "BCFZ") == 0 || data.compare(0, 4, "BCFS") == 0){
        // Guitar Pro 6: the score in a file system of its own, usually compressed
        std::string fileSystem, score;
        if (data.compare(0, 4, "BCFZ") == 0){
            if (!unpackBcfz(data, fileSystem, error)) return false;
        } else {
            fileSystem = data;
        }
        if (!bcfsFile(fileSystem, "score.gpif", score)){
            error = "no score inside it (score.gpif)";
            return false;
        }
        return readGpif(score, out, error);
    }
    if (data.size() > 1 && data.compare(1, 18, "FICHIER GUITAR PRO") == 0){
        error = "a Guitar Pro 3 to 5 file: not yet";
        return false;
    }
    error = "not a Guitar Pro file";
    return false;
}
