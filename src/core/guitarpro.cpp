#include "core/guitarpro.h"

#include "core/xml.h"
#include "miniz.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
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

// --- A score, whichever file it came from ----------------------------------------------------------------------
// The XML scores (7 and 8, and 6) and the binary files (3 to 5) are both read into this, and a chart made from it

struct GpNote {
    int id;                   // one per note written in the file, however often a repeat plays it
    int string;               // into its track's pitches, as the file lists them
    int fret;
    bool tieDestination = false;
    bool dead = false;
    bool graceBefore = false;  // a grace note played just before it (Guitar Pro 3 to 5 keep them on the note)
};

struct GpBeat {
    int id;
    int duration = RESOLUTION; // ticks
    bool grace = false;       // played before its beat, in no time of its own
    std::vector<GpNote> notes;
};

using GpVoice = std::vector<GpBeat>;

struct GpTrack {
    std::string name;
    std::vector<int> pitches; // by the file's string numbers
    std::string instrument;   // what the file calls it, lowercase: "electricbass" (7 and 8), "e-bass4" (6), "bass" (from a
                              // MIDI program, 3 to 5), "" if it doesn't say
};

struct MasterBar {
    int beats = 4, beatUnit = 4;
    int fifths = 0;
    bool minor = false;
    bool repeatStart = false, repeatEnd = false;
    int repeatCount = 2;              // how many times the repeated part plays
    std::vector<int> endings;         // alternate endings: the passes this bar plays on (empty: every pass)
    std::vector<std::vector<GpVoice>> tracks; // each track's voices in this bar
};

struct GpTempo {
    int bar;
    double position;          // a share of the bar, 0 at its start
    double bpm;               // quarter notes a minute
};

struct GpScore {
    std::string title, artist;
    std::vector<GpTrack> tracks;
    std::vector<MasterBar> masterBars;
    std::vector<GpTempo> tempos;
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

bool contains(const std::string& text, const char* part){ return text.find(part) != std::string::npos; }

bool buildChart(const GpScore& score, GuitarProImport& out, std::string& error){
    out = GuitarProImport{};
    Chart& chart = out.chart;
    chart.version = 2;
    chart.title = score.title;
    chart.artist = score.artist;
    chart.resolution = RESOLUTION;
    chart.offset = 0.0;
    if (score.masterBars.empty()){
        error = "the score has no bars";
        return false;
    }

    // Which tracks come in: guitars and basses, by their instrument (every track has a tuning in a file, a flute's
    // too); a file that doesn't name its instruments, by having strings
    std::vector<int> kept(score.tracks.size(), -1); // the chart track each file track became
    for (size_t t = 0; t < score.tracks.size(); t++){
        const GpTrack& track = score.tracks[t];
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
    for (int index : playOrder(score.masterBars)){
        const MasterBar& bar = score.masterBars[index];
        const int length = bar.beats * RESOLUTION * 4 / bar.beatUnit;
        if (bar.beats != lastBeats || bar.beatUnit != lastUnit) chart.timeSignatures.push_back({ tick, bar.beats, bar.beatUnit });
        if (bar.fifths != lastFifths || bar.minor != lastMinor) chart.keys.push_back({ tick, KeySignature{ bar.fifths, bar.minor } });
        lastBeats = bar.beats; lastUnit = bar.beatUnit; lastFifths = bar.fifths; lastMinor = bar.minor;
        for (const GpTempo& tempo : score.tempos){
            if (tempo.bar != index) continue;
            int at = tick + (int)std::lround(std::clamp(tempo.position, 0.0, 1.0) * length);
            if (!chart.tempoMap.empty() && chart.tempoMap.back().tick == at) chart.tempoMap.back().bpm = tempo.bpm;
            else if (chart.tempoMap.empty() || chart.tempoMap.back().bpm != tempo.bpm) chart.tempoMap.push_back({ at, tempo.bpm });
        }

        for (size_t t = 0; t < score.tracks.size() && t < bar.tracks.size(); t++){
            if (kept[t] < 0) continue;
            FrettedTrack& fretted = chart.frettedTracks[kept[t]];
            const GpTrack& track = score.tracks[t];
            for (const GpVoice& voice : bar.tracks[t]){
                int at = tick;
                for (const GpBeat& beat : voice){
                    if (beat.grace){
                        graceBeats.insert(beat.id);
                        continue;
                    }
                    for (const GpNote& note : beat.notes){
                        if (note.graceBefore) graceBeats.insert(-1 - note.id); // the note's own, apart from beats' ids
                        if (note.dead){ deadNotes.insert(note.id); continue; }
                        if (note.string < 0 || note.string >= (int)track.pitches.size() || note.fret < 0 || note.fret > MAX_FRET) continue;
                        // Its string, counted from the lowest
                        int pitch = track.pitches[note.string];
                        int stringIndex = (int)(std::find(fretted.tuning.begin(), fretted.tuning.end(), pitch) - fretted.tuning.begin());
                        if (note.tieDestination){
                            // Tied on: the note before on that string rings longer
                            for (auto it = fretted.notes.rbegin(); it != fretted.notes.rend(); ++it){
                                if (it->stringIndex != stringIndex) continue;
                                it->duration = at + beat.duration - it->tick;
                                break;
                            }
                            continue;
                        }
                        fretted.notes.push_back({ at, stringIndex, note.fret, beat.duration });
                    }
                    at += beat.duration;
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

// --- Guitar Pro 6 to 8: the XML score --------------------------------------------------------------------------

// The instrument a track is for, as the file names it: Guitar Pro 7 and 8 say "electricGuitar", "steelGuitar",
// "electricBass", "violin", "drumKit"; Guitar Pro 6 "e-gtr6", "s-gtr6", "e-bass4", "vln", "drmkt"
std::string instrumentOf(const XmlNode& track){
    std::string kind;
    if (const XmlNode* set = track.child("InstrumentSet")) kind = set->childText("Type");
    if (kind.empty()) if (const XmlNode* instrument = track.child("Instrument")) kind = instrument->attribute("ref");
    std::transform(kind.begin(), kind.end(), kind.begin(), [](unsigned char c){ return (char)std::tolower(c); });
    return kind;
}

bool gpifScore(const std::string& xml, GpScore& score, std::string& error){
    XmlNode root;
    if (!parseXml(xml, root, error)){
        error = "the score can't be read (" + error + ")";
        return false;
    }
    if (root.name != "GPIF"){
        error = "not a Guitar Pro score";
        return false;
    }
    if (const XmlNode* info = root.child("Score")){
        score.title = info->childText("Title");
        score.artist = info->childText("Artist");
    }

    // The tracks, in the order the master bars list their bars
    std::map<int, const XmlNode*> trackNodes = byId(root, "Tracks", "Track");
    std::vector<int> trackOrder;
    if (const XmlNode* master = root.child("MasterTrack")) trackOrder = numbers(master->childText("Tracks"));
    if (trackOrder.empty()) for (const auto& [id, node] : trackNodes) trackOrder.push_back(id);
    for (int id : trackOrder){
        GpTrack track;
        auto found = trackNodes.find(id);
        if (found != trackNodes.end()){
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
        }
        score.tracks.push_back(track);
    }

    // Tempos, by bar and where in it (a share of the bar); "120 2" is 120 quarter notes a minute
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
                if (bpm > 0.0) score.tempos.push_back({ std::atoi(automation->childText("Bar").c_str()), std::atof(automation->childText("Position").c_str()), bpm });
            }
        }
    }

    // The notes, by the ids the bars point through: bars to voices to beats to notes
    std::map<int, const XmlNode*> barNodes = byId(root, "Bars", "Bar"), voiceNodes = byId(root, "Voices", "Voice");
    std::map<int, const XmlNode*> beatNodes = byId(root, "Beats", "Beat"), noteNodes = byId(root, "Notes", "Note");
    std::map<int, const XmlNode*> rhythmNodes = byId(root, "Rhythms", "Rhythm");
    auto beatOf = [&](int beatId){
        GpBeat beat;
        beat.id = beatId;
        auto node = beatNodes.find(beatId);
        if (node == beatNodes.end()) return beat;
        const XmlNode* rhythmRef = node->second->child("Rhythm");
        auto rhythm = rhythmRef ? rhythmNodes.find(std::atoi(rhythmRef->attribute("ref").c_str())) : rhythmNodes.end();
        beat.duration = rhythmTicks(rhythm == rhythmNodes.end() ? nullptr : rhythm->second);
        beat.grace = node->second->child("GraceNotes") != nullptr;
        for (int noteId : numbers(node->second->childText("Notes"))){
            auto note = noteNodes.find(noteId);
            if (note == noteNodes.end()) continue;
            const XmlNode* stringProperty = property(*note->second, "String");
            const XmlNode* fretProperty = property(*note->second, "Fret");
            if (!stringProperty || !fretProperty) continue;
            GpNote played{ noteId, std::atoi(stringProperty->childText("String").c_str()), std::atoi(fretProperty->childText("Fret").c_str()) };
            const XmlNode* tie = note->second->child("Tie");
            played.tieDestination = tie && tie->attribute("destination") == "true";
            played.dead = property(*note->second, "Muted") != nullptr;
            beat.notes.push_back(played);
        }
        return beat;
    };

    // The master bars: their time and key, their repeats, and each track's bar
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
            if (const XmlNode* repeat = node->child("Repeat")){
                bar.repeatStart = repeat->attribute("start") == "true";
                bar.repeatEnd = repeat->attribute("end") == "true";
                bar.repeatCount = std::max(2, std::atoi(repeat->attribute("count").c_str()));
            }
            bar.endings = numbers(node->childText("AlternateEndings"));
            for (int barId : numbers(node->childText("Bars"))){
                std::vector<GpVoice> voices;
                auto barNode = barNodes.find(barId);
                if (barNode != barNodes.end()){
                    for (int voiceId : numbers(barNode->second->childText("Voices"))){
                        auto voice = voiceNodes.find(voiceId);
                        if (voiceId < 0 || voice == voiceNodes.end()) continue;
                        GpVoice beats;
                        for (int beatId : numbers(voice->second->childText("Beats"))) beats.push_back(beatOf(beatId));
                        voices.push_back(beats);
                    }
                }
                bar.tracks.push_back(voices);
            }
            score.masterBars.push_back(bar);
        }
    }
    return true;
}

// --- Guitar Pro 3 to 5: the binary files -----------------------------------------------------------------------
// Little-endian numbers and strings of a few kinds, read in the order Guitar Pro writes them; everything that isn't
// the music (page setup, sound settings, chord diagrams, effects) is read past. Its layout follows alphaTab's reader
// (MPL-2.0), which knows these files best.

struct Bytes {
    const std::string& data;
    size_t at = 0;
    bool failed = false;
    bool has(size_t count){
        if (at + count > data.size()){ failed = true; at = data.size(); return false; }
        return true;
    }
    int u8(){ return has(1) ? (unsigned char)data[at++] : 0; }
    int s8(){ return has(1) ? (signed char)data[at++] : 0; }
    int i16(){ if (!has(2)) return 0; int v = (short)((unsigned char)data[at] | ((unsigned char)data[at + 1] << 8)); at += 2; return v; }
    int i32(){
        if (!has(4)) return 0;
        uint32_t v = (unsigned char)data[at] | ((unsigned char)data[at + 1] << 8) | ((unsigned char)data[at + 2] << 16) | ((uint32_t)(unsigned char)data[at + 3] << 24);
        at += 4;
        return (int)v;
    }
    void skip(int count){ if (count > 0 && has((size_t)count)) at += count; }
    std::string text(int count){
        if (count <= 0 || !has((size_t)count)) return "";
        std::string value = data.substr(at, count);
        at += count;
        return value;
    }
    // A byte of length, then a field that many bytes long, or `size` whatever the length
    std::string fixed(int size){ int length = u8(); std::string field = text(size); return field.substr(0, std::min<size_t>(length, field.size())); }
    // A four-byte size not used, then a byte of length, then the text
    std::string intUnused(){ skip(4); return text(u8()); }
    std::string intSized(){ return text(i32()); }
    // A four-byte size (the text and its length byte), the length byte, then the text
    std::string intByte(){ int length = i32() - 1; u8(); return text(length); }
};

// Text as Guitar Pro 3 to 5 wrote it: Windows' Western European code page, turned into UTF-8
std::string fromLatin1(const std::string& text){
    std::string out;
    for (unsigned char c : text){
        if (c < 0x80) out += (char)c;
        else { out += (char)(0xC0 | (c >> 6)); out += (char)(0x80 | (c & 0x3F)); }
    }
    return out;
}

// The instrument a MIDI program is: guitars are 24 to 31, basses 32 to 39
std::string programInstrument(int program){
    if (program >= 24 && program <= 31) return "guitar";
    if (program >= 32 && program <= 39) return "bass";
    if (program < 0) return "";
    return "program " + std::to_string(program);
}

bool binaryScore(const std::string& data, GpScore& score, std::string& error){
    Bytes in{ data };
    const std::string version = in.fixed(30);
    const std::string prefix = "FICHIER GUITAR PRO v";
    if (version.compare(0, prefix.size(), prefix) != 0){
        error = "not a Guitar Pro file";
        return false;
    }
    std::string number = version.substr(prefix.size());
    size_t dot = number.find('.');
    const int v = 100 * std::atoi(number.substr(0, dot).c_str()) + (dot == std::string::npos ? 0 : std::atoi(number.substr(dot + 1).c_str()));
    if (v < 300 || v >= 600){
        error = "Guitar Pro " + number + " isn't a version lahn knows";
        return false;
    }

    // What the song is
    score.title = fromLatin1(in.intUnused());
    in.intUnused();                              // subtitle
    score.artist = fromLatin1(in.intUnused());
    in.intUnused();                              // album
    in.intUnused();                              // words
    if (v >= 500) in.intUnused();                // music
    in.intUnused();                              // copyright
    in.intUnused();                              // tab
    in.intUnused();                              // instructions
    int noticeLines = in.i32();
    for (int i = 0; i < noticeLines && i < 1000 && !in.failed; i++) in.intUnused();
    if (v < 500) in.u8();                        // triplet feel
    if (v >= 400){                               // lyrics
        in.i32();
        for (int i = 0; i < 5; i++){ in.i32(); in.intSized(); }
    }
    if (v >= 510) in.skip(19);                   // the master's sound
    if (v >= 500){                               // the page, and the tempo's name
        in.skip(28);
        in.i16();
        for (int i = 0; i < 10; i++) in.intByte();
        in.intByte();
    }
    double tempo = in.i32();
    if (v >= 510) in.u8();                       // tempo hidden
    in.i32();                                    // key
    if (v >= 400) in.u8();                       // octave
    std::vector<int> programs(64);               // MIDI channels: the instrument on each
    for (int i = 0; i < 64; i++){
        programs[i] = in.i32();
        in.skip(8);
    }
    if (v >= 500) in.skip(19 * 2 + 4);           // where the jumps go (segno, coda...), and 4 more
    const int barCount = in.i32(), trackCount = in.i32();
    if (in.failed || barCount < 1 || barCount > 5000 || trackCount < 1 || trackCount > 100){
        error = "the file is damaged (" + std::to_string(barCount) + " bars, " + std::to_string(trackCount) + " tracks)";
        return false;
    }
    if (tempo > 0) score.tempos.push_back({ 0, 0.0, tempo });

    // The master bars: time, repeats, alternate endings, key
    std::vector<int> endingMasks(barCount, 0);
    for (int b = 0; b < barCount && !in.failed; b++){
        MasterBar bar;
        if (b > 0){
            const MasterBar& previous = score.masterBars.back();
            bar.beats = previous.beats; bar.beatUnit = previous.beatUnit;
            bar.fifths = previous.fifths; bar.minor = previous.minor;
        }
        int flags = in.u8();
        if (flags & 0x01) bar.beats = std::max(1, in.u8());
        if (flags & 0x02) bar.beatUnit = std::max(1, in.u8());
        bar.repeatStart = flags & 0x04;
        if (flags & 0x08){
            bar.repeatCount = in.u8() + (v >= 500 ? 0 : 1);
            bar.repeatEnd = bar.repeatCount > 0;
        }
        if ((flags & 0x10) && v < 500){
            // Before Guitar Pro 5 an ending is a count: this bar takes the passes up to it that the endings before it
            // in the same repeat haven't
            int taken = 0;
            for (int p = b - 1; p >= 0; p--){
                const MasterBar& before = score.masterBars[p];
                if (before.repeatEnd && p != b - 1) break;
                taken |= endingMasks[p];
                if (before.repeatStart) break;
            }
            int count = in.u8();
            for (int i = 0; i < 8; i++) if (count > i && !(taken & (1 << i))) endingMasks[b] |= 1 << i;
        }
        if (flags & 0x20){ in.intByte(); in.skip(4); } // a marker, and its color
        if (flags & 0x40){ bar.fifths = std::clamp(in.s8(), -7, 7); bar.minor = in.u8() == 1; }
        if (v >= 500 && (flags & 0x03)) in.skip(4);    // beaming
        if (v >= 500) endingMasks[b] = in.u8();
        if (v >= 500){ in.u8(); in.u8(); }            // triplet feel, and a byte
        for (int i = 0; i < 8; i++) if (endingMasks[b] & (1 << i)) bar.endings.push_back(i + 1);
        bar.tracks.resize(trackCount);
        score.masterBars.push_back(bar);
    }

    // The tracks: name, strings, and the instrument on their channel
    std::vector<bool> drums(trackCount);
    for (int t = 0; t < trackCount && !in.failed; t++){
        GpTrack track;
        int flags = in.u8();
        drums[t] = flags & 0x01;
        track.name = fromLatin1(in.fixed(40));
        int strings = in.i32();
        for (int i = 0; i < 7; i++){ int pitch = in.i32(); if (i < strings) track.pitches.push_back(pitch); }
        in.i32();                                // port
        int channel = in.i32() - 1;
        in.i32();                                // effect channel
        in.skip(4);                              // frets
        in.i32();                                // capo
        in.skip(4);                              // color
        if (v >= 500){
            in.skip(5);                          // staff, MIDI and sound flags, bank, humanizing
            in.skip(4 + 4 + 4 + 10 + 1 + 1);     // clef mode, and what no setting seems to change
            in.skip(16);                         // the sound bank
            if (v >= 510){ in.skip(4); in.intByte(); in.intByte(); }
        }
        int program = channel >= 0 && channel < 64 ? programs[channel] : -1;
        track.instrument = drums[t] || channel == 9 ? "drums" : programInstrument(program);
        score.tracks.push_back(track);
    }

    // The bars, each track's in turn: voices of beats, each beat's notes by string
    int ids = 0;
    for (int b = 0; b < barCount && !in.failed; b++){
        MasterBar& bar = score.masterBars[b];
        const int barLength = bar.beats * RESOLUTION * 4 / bar.beatUnit;
        for (int t = 0; t < trackCount && !in.failed; t++){
            const int strings = (int)score.tracks[t].pitches.size();
            int voices = 1;
            if (v >= 500){ in.u8(); voices = 2; }
            for (int voiceIndex = 0; voiceIndex < voices && !in.failed; voiceIndex++){
                int beatCount = in.i32();
                if (beatCount < 0 || beatCount > 1000){ in.failed = true; break; }
                GpVoice voice;
                int offset = 0;
                for (int i = 0; i < beatCount && !in.failed; i++){
                    GpBeat beat;
                    beat.id = ids++;
                    int flags = in.u8();
                    if (flags & 0x40) in.u8(); // empty or a rest: either way, its time passes
                    int value = in.s8();
                    static const double QUARTERS[] = { 4.0, 2.0, 1.0, 0.5, 0.25, 0.125, 0.0625 };
                    double quarters = value >= -2 && value <= 4 ? QUARTERS[value + 2] : 1.0;
                    if (flags & 0x01) quarters *= 1.5;
                    if (flags & 0x20){
                        int tuplet = in.i32();
                        int times = tuplet == 3 ? 2 : (tuplet >= 5 && tuplet <= 7) ? 4 : (tuplet >= 9 && tuplet <= 13) ? 8 : tuplet;
                        if (tuplet > 0 && times > 0) quarters = quarters * times / tuplet;
                    }
                    beat.duration = std::max(1, (int)std::lround(quarters * RESOLUTION));
                    if (flags & 0x02){       // a chord diagram
                        if (v >= 500){ in.skip(17); in.fixed(21); in.skip(4); in.i32(); in.skip(28); in.u8(); in.skip(5); in.skip(26); }
                        else if (in.u8() != 0){
                            if (v >= 400){ in.skip(16); in.fixed(21); in.skip(4); in.i32(); in.skip(28); in.u8(); in.skip(5); in.skip(26); }
                            else { in.skip(25); in.fixed(34); in.i32(); in.skip(24); in.skip(36); }
                        } else {
                            in.intByte();
                            if (in.i32() > 0) in.skip(4 * (v >= 406 ? 7 : 6));
                        }
                    }
                    if (flags & 0x04) in.intUnused(); // text
                    if (flags & 0x08){       // the beat's effects
                        int effects = in.u8(), effects2 = v >= 400 ? in.u8() : 0;
                        if (effects & 0x20){ in.s8(); if (v < 400) in.skip(4); }
                        if (effects2 & 0x04){ in.u8(); in.i32(); int points = in.i32(); if (points < 0 || points > 1000) in.failed = true; else in.skip(points * 9); }
                        if (effects & 0x40) in.skip(2);
                        if (effects2 & 0x02) in.s8();
                    }
                    if (flags & 0x10){       // a mix table: a change of tempo, among sound settings
                        in.s8();
                        if (v >= 500) in.skip(16);
                        int changes[6];
                        for (int& change : changes) change = in.s8();
                        if (v >= 500) in.intByte();
                        int newTempo = in.i32();
                        for (int change : changes) if (change >= 0) in.u8();
                        if (newTempo >= 0){ in.s8(); if (v >= 510) in.u8(); }
                        if (v >= 400) in.u8();
                        if (v >= 500) in.s8();
                        if (v >= 510){ in.intByte(); in.intByte(); }
                        if (newTempo > 0) score.tempos.push_back({ b, (double)offset / barLength, (double)newTempo });
                    }
                    int stringBits = in.u8();
                    for (int s = 6; s >= 0 && !in.failed; s--){
                        if (!(stringBits & (1 << s)) || 6 - s >= strings) continue;
                        GpNote note{ ids++, 6 - s, 0 };
                        int noteFlags = in.u8();
                        if (noteFlags & 0x20){
                            int type = in.u8();
                            note.tieDestination = type == 2;
                            note.dead = type == 3;
                        }
                        if ((noteFlags & 0x01) && v < 500) in.skip(2);
                        if (noteFlags & 0x10) in.s8();       // dynamics
                        if (noteFlags & 0x20) note.fret = in.s8();
                        if (noteFlags & 0x80) in.skip(2);     // fingering
                        if (v >= 500){
                            if (noteFlags & 0x01) in.skip(8); // how long it rings, as a share
                            in.u8();
                        }
                        if (noteFlags & 0x08){               // the note's effects
                            int effects = in.u8(), effects2 = v >= 400 ? in.u8() : 0;
                            if (effects & 0x01){ in.u8(); in.i32(); int points = in.i32(); if (points < 0 || points > 1000) in.failed = true; else in.skip(points * 9); }
                            if (effects & 0x10){ in.skip(4); if (v >= 500) in.u8(); note.graceBefore = true; }
                            if (effects2 & 0x04) in.u8();
                            if (effects2 & 0x08) in.s8();
                            if (effects2 & 0x10){
                                int harmonic = in.u8();
                                if (v >= 500 && harmonic == 2) in.skip(3);
                                if (v >= 500 && harmonic == 3) in.skip(1);
                            }
                            if (effects2 & 0x20) in.skip(2);
                        }
                        beat.notes.push_back(note);
                    }
                    if (v >= 500 && (in.i16() & 0x800)) in.u8();
                    offset += beat.duration;
                    voice.push_back(beat);
                }
                bar.tracks[t].push_back(voice);
            }
        }
    }
    if (in.failed){
        error = "the file is damaged, or of a kind lahn reads wrong (it ends too soon)";
        return false;
    }
    return true;
}

} // namespace

bool readGpif(const std::string& xml, GuitarProImport& out, std::string& error){
    GpScore score;
    return gpifScore(xml, score, error) && buildChart(score, out, error);
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
        // Guitar Pro 3 to 5: their own binary layout
        GpScore score;
        return binaryScore(data, score, error) && buildChart(score, out, error);
    }
    error = "not a Guitar Pro file";
    return false;
}
