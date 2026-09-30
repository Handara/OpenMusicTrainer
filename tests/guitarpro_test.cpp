#include "doctest/doctest.h"

#include "core/backing.h"
#include "core/guitarpro.h"
#include "core/songlibrary.h"
#include "core/xml.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>

TEST_CASE("xml: elements, attributes, text, CDATA and entities"){
    XmlNode root;
    std::string error;
    REQUIRE_MESSAGE(parseXml("\xEF\xBB\xBF<?xml version=\"1.0\"?>\n<!-- a comment -->\n"
                             "<Score kind='test' size=\"2\">\n"
                             "  <Title><![CDATA[Rock & <Roll>]]></Title>\n"
                             "  <Artist>Tom &amp; Jerry &#233;</Artist>\n"
                             "  <Empty/>\n"
                             "  <Note id=\"3\"><Fret>5</Fret></Note><Note id=\"4\"/>\n"
                             "</Score>\n", root, error), error);
    CHECK(root.name == "Score");
    CHECK(root.attribute("kind") == "test");
    CHECK(root.attribute("missing") == "");
    CHECK(root.childText("Title") == "Rock & <Roll>");
    CHECK(root.childText("Artist") == "Tom & Jerry \xC3\xA9");
    REQUIRE(root.child("Empty") != nullptr);
    CHECK(root.childrenNamed("Note").size() == 2);
    CHECK(root.childrenNamed("Note")[0]->childText("Fret") == "5");

    CHECK_FALSE(parseXml("<a><b></a>", root, error));
    CHECK(error.find("closes") != std::string::npos);
    CHECK_FALSE(parseXml("<a>", root, error));
    CHECK_FALSE(parseXml("not xml", root, error));
}

// A small score: a guitar, a bass and drums; a repeated first part (4/4, then 3/4); triplets, a chord, a tie over the
// bar line, a grace note, and a tempo change halfway through the last bar, counted in eighths
static const char* SCORE = R"(<?xml version="1.0" encoding="utf-8"?>
<GPIF>
  <Score><Title><![CDATA[Test Song]]></Title><Artist>Someone</Artist></Score>
  <MasterTrack>
    <Tracks>0 1 2</Tracks>
    <Automations>
      <Automation><Type>Tempo</Type><Bar>0</Bar><Position>0</Position><Value>100 2</Value></Automation>
      <Automation><Type>Tempo</Type><Bar>2</Bar><Position>0.5</Position><Value>60 1</Value></Automation>
    </Automations>
  </MasterTrack>
  <Tracks>
    <Track id="0"><Name>Lead</Name>
      <Staves><Staff><Properties><Property name="Tuning"><Pitches>40 45 50 55 59 64</Pitches></Property></Properties></Staff></Staves>
    </Track>
    <Track id="1"><Name>Low end</Name><Properties><Property name="Tuning"><Pitches>28 33 38 43</Pitches></Property></Properties></Track>
    <Track id="2"><Name>Drums</Name><InstrumentSet><Type>drumKit</Type></InstrumentSet></Track>
  </Tracks>
  <MasterBars>
    <MasterBar><Key><AccidentalCount>1</AccidentalCount><Mode>Major</Mode></Key><Time>4/4</Time><Bars>0 1 2</Bars>
      <Repeat start="true" end="false" count="0"/></MasterBar>
    <MasterBar><Key><AccidentalCount>1</AccidentalCount><Mode>Major</Mode></Key><Time>3/4</Time><Bars>3 4 5</Bars>
      <Repeat start="false" end="true" count="2"/></MasterBar>
    <MasterBar><Key><AccidentalCount>-3</AccidentalCount><Mode>Minor</Mode></Key><Time>4/4</Time><Bars>6 7 8</Bars></MasterBar>
  </MasterBars>
  <Bars>
    <Bar id="0"><Voices>0 -1 -1 -1</Voices></Bar>
    <Bar id="1"><Voices>1 -1 -1 -1</Voices></Bar>
    <Bar id="2"><Voices>-1 -1 -1 -1</Voices></Bar>
    <Bar id="3"><Voices>2 -1 -1 -1</Voices></Bar>
    <Bar id="4"><Voices>-1 -1 -1 -1</Voices></Bar>
    <Bar id="5"><Voices>-1 -1 -1 -1</Voices></Bar>
    <Bar id="6"><Voices>3 -1 -1 -1</Voices></Bar>
    <Bar id="7"><Voices>4 -1 -1 -1</Voices></Bar>
    <Bar id="8"><Voices>-1 -1 -1 -1</Voices></Bar>
  </Bars>
  <Voices>
    <Voice id="0"><Beats>0 1 2</Beats></Voice>
    <Voice id="1"><Beats>3</Beats></Voice>
    <Voice id="2"><Beats>9 4 5 6 7</Beats></Voice>
    <Voice id="3"><Beats>8</Beats></Voice>
    <Voice id="4"><Beats>10</Beats></Voice>
  </Voices>
  <Beats>
    <Beat id="0"><Rhythm ref="0"/><Notes>0</Notes></Beat>
    <Beat id="1"><Rhythm ref="0"/><Notes>1 2</Notes></Beat>
    <Beat id="2"><Rhythm ref="1"/></Beat>
    <Beat id="3"><Rhythm ref="2"/><Notes>3</Notes></Beat>
    <Beat id="4"><Rhythm ref="3"/><Notes>4</Notes></Beat>
    <Beat id="5"><Rhythm ref="3"/><Notes>4</Notes></Beat>
    <Beat id="6"><Rhythm ref="3"/><Notes>4</Notes></Beat>
    <Beat id="7"><Rhythm ref="1"/><Notes>5</Notes></Beat>
    <Beat id="8"><Rhythm ref="2"/><Notes>6</Notes></Beat>
    <Beat id="9"><GraceNotes>BeforeBeat</GraceNotes><Rhythm ref="4"/><Notes>4</Notes></Beat>
    <Beat id="10"><Rhythm ref="2"/><Notes>7</Notes></Beat>
  </Beats>
  <Notes>
    <Note id="0"><Properties><Property name="String"><String>5</String></Property><Property name="Fret"><Fret>3</Fret></Property></Properties></Note>
    <Note id="1"><Properties><Property name="String"><String>0</String></Property><Property name="Fret"><Fret>3</Fret></Property></Properties></Note>
    <Note id="2"><Properties><Property name="String"><String>1</String></Property><Property name="Fret"><Fret>2</Fret></Property></Properties></Note>
    <Note id="3"><Properties><Property name="String"><String>0</String></Property><Property name="Fret"><Fret>5</Fret></Property></Properties></Note>
    <Note id="4"><Properties><Property name="String"><String>2</String></Property><Property name="Fret"><Fret>5</Fret></Property></Properties></Note>
    <Note id="5"><Tie origin="true" destination="false"/><Properties><Property name="String"><String>3</String></Property><Property name="Fret"><Fret>0</Fret></Property></Properties></Note>
    <Note id="6"><Tie origin="false" destination="true"/><Properties><Property name="String"><String>3</String></Property><Property name="Fret"><Fret>0</Fret></Property></Properties></Note>
    <Note id="7"><Properties><Property name="String"><String>3</String></Property><Property name="Fret"><Fret>2</Fret></Property></Properties></Note>
  </Notes>
  <Rhythms>
    <Rhythm id="0"><NoteValue>Quarter</NoteValue></Rhythm>
    <Rhythm id="1"><NoteValue>Half</NoteValue></Rhythm>
    <Rhythm id="2"><NoteValue>Whole</NoteValue></Rhythm>
    <Rhythm id="3"><NoteValue>Eighth</NoteValue><PrimaryTuplet num="3" den="2"/></Rhythm>
    <Rhythm id="4"><NoteValue>16th</NoteValue></Rhythm>
  </Rhythms>
</GPIF>)";

TEST_CASE("a Guitar Pro score becomes a chart: its guitar and bass, repeats played out"){
    GuitarProImport import;
    std::string error;
    REQUIRE_MESSAGE(readGpif(SCORE, import, error), error);
    const Chart& chart = import.chart;
    CHECK(chart.title == "Test Song");
    CHECK(chart.artist == "Someone");
    CHECK(chart.resolution == 480);

    // The bars as played: the first two twice (the repeat), then the last. 4/4 is 1920 ticks, 3/4 is 1440.
    CHECK(chart.endTick == 1920 + 1440 + 1920 + 1440 + 1920);
    REQUIRE(chart.timeSignatures.size() == 5);
    CHECK(chart.timeSignatures[1].tick == 1920);
    CHECK(chart.timeSignatures[1].beats == 3);
    CHECK(chart.timeSignatures[4].tick == 6720);
    REQUIRE(chart.keys.size() == 2);
    CHECK(chart.keys[0].key.fifths == 1);
    CHECK(chart.keys[1].tick == 6720);
    CHECK(chart.keys[1].key.fifths == -3);
    CHECK(chart.keys[1].key.minor);

    // 100 quarters a minute from the top; halfway through the last bar, 60 eighths: 30 quarters
    REQUIRE(chart.tempoMap.size() == 2);
    CHECK(chart.tempoMap[0].bpm == doctest::Approx(100.0));
    CHECK(chart.tempoMap[1].tick == 6720 + 960);
    CHECK(chart.tempoMap[1].bpm == doctest::Approx(30.0));

    // The drums are left out, and the grace note; the rest come in
    REQUIRE(chart.frettedTracks.size() == 2);
    CHECK(import.leftOut.size() == 2); // the drums, and a grace note
    const FrettedTrack& guitar = chart.frettedTracks[0];
    const FrettedTrack& bass = chart.frettedTracks[1];
    CHECK(guitar.type == InstrumentType::Guitar);
    CHECK(guitar.name == "Lead");
    CHECK(bass.type == InstrumentType::Bass); // by its low tuning: its name doesn't say
    CHECK(bass.tuning == std::vector<int>{ 28, 33, 38, 43 });

    // The guitar, first time through: a note on the high E, a chord, a rest; triplets; a half note tied on
    std::vector<FrettedNote> expected = {
        { 0, 5, 3, 480 }, { 480, 0, 3, 480 }, { 480, 1, 2, 480 },
        { 1920, 2, 5, 160 }, { 2080, 2, 5, 160 }, { 2240, 2, 5, 160 }, { 2400, 3, 0, 960 },
        { 3360, 5, 3, 480 }, { 3840, 0, 3, 480 }, { 3840, 1, 2, 480 },
        { 5280, 2, 5, 160 }, { 5440, 2, 5, 160 }, { 5600, 2, 5, 160 },
        { 5760, 3, 0, 960 + 1920 }, // the second time, tied into the last bar: one note, ringing on
    };
    REQUIRE(guitar.notes.size() == expected.size());
    for (size_t i = 0; i < expected.size(); i++){
        CAPTURE(i);
        CHECK(guitar.notes[i].tick == expected[i].tick);
        CHECK(guitar.notes[i].stringIndex == expected[i].stringIndex);
        CHECK(guitar.notes[i].fret == expected[i].fret);
        CHECK(guitar.notes[i].duration == expected[i].duration);
    }
    // The bass: a whole note each time through the first bar, and one in the last
    REQUIRE(bass.notes.size() == 3);
    CHECK(bass.notes[0].tick == 0);
    CHECK(bass.notes[0].fret == 5);
    CHECK(bass.notes[1].tick == 3360);
    CHECK(bass.notes[2].tick == 6720);
    CHECK(bass.notes[2].stringIndex == 3);
}

TEST_CASE("alternate endings: each pass plays its own"){
    // |: A | B (1st time) :| C (2nd time) | D
    const char* score = R"(<GPIF><MasterTrack><Tracks>0</Tracks></MasterTrack>
      <Tracks><Track id="0"><Name>Bass</Name><Properties><Property name="Tuning"><Pitches>28 33 38 43</Pitches></Property></Properties></Track></Tracks>
      <MasterBars>
        <MasterBar><Time>4/4</Time><Bars>0</Bars><Repeat start="true" end="false" count="0"/></MasterBar>
        <MasterBar><Time>4/4</Time><Bars>1</Bars><AlternateEndings>1</AlternateEndings><Repeat start="false" end="true" count="2"/></MasterBar>
        <MasterBar><Time>4/4</Time><Bars>2</Bars><AlternateEndings>2</AlternateEndings></MasterBar>
        <MasterBar><Time>4/4</Time><Bars>3</Bars></MasterBar>
      </MasterBars>
      <Bars><Bar id="0"><Voices>0</Voices></Bar><Bar id="1"><Voices>1</Voices></Bar><Bar id="2"><Voices>2</Voices></Bar><Bar id="3"><Voices>3</Voices></Bar></Bars>
      <Voices><Voice id="0"><Beats>0</Beats></Voice><Voice id="1"><Beats>1</Beats></Voice><Voice id="2"><Beats>2</Beats></Voice><Voice id="3"><Beats>3</Beats></Voice></Voices>
      <Beats><Beat id="0"><Rhythm ref="0"/><Notes>0</Notes></Beat><Beat id="1"><Rhythm ref="0"/><Notes>1</Notes></Beat>
             <Beat id="2"><Rhythm ref="0"/><Notes>2</Notes></Beat><Beat id="3"><Rhythm ref="0"/><Notes>3</Notes></Beat></Beats>
      <Notes>
        <Note id="0"><Properties><Property name="String"><String>0</String></Property><Property name="Fret"><Fret>1</Fret></Property></Properties></Note>
        <Note id="1"><Properties><Property name="String"><String>0</String></Property><Property name="Fret"><Fret>2</Fret></Property></Properties></Note>
        <Note id="2"><Properties><Property name="String"><String>0</String></Property><Property name="Fret"><Fret>3</Fret></Property></Properties></Note>
        <Note id="3"><Properties><Property name="String"><String>0</String></Property><Property name="Fret"><Fret>4</Fret></Property></Properties></Note>
      </Notes>
      <Rhythms><Rhythm id="0"><NoteValue>Whole</NoteValue></Rhythm></Rhythms></GPIF>)";
    GuitarProImport import;
    std::string error;
    REQUIRE_MESSAGE(readGpif(score, import, error), error);
    std::vector<int> frets;
    for (const FrettedNote& note : import.chart.frettedTracks[0].notes) frets.push_back(note.fret);
    CHECK(frets == std::vector<int>{ 1, 2, 1, 3, 4 }); // A B A C D
    CHECK(import.chart.frettedTracks[0].type == InstrumentType::Bass);
    CHECK(import.chart.tempoMap.front().bpm == doctest::Approx(120.0)); // no tempo in the file: 120
}

TEST_CASE("what isn't a Guitar Pro score, or has nothing to play, says so"){
    GuitarProImport import;
    std::string error;
    CHECK_FALSE(readGpif("<Score/>", import, error));
    CHECK_FALSE(readGpif("<GPIF><MasterBars/></GPIF>", import, error));
    CHECK_FALSE(importGuitarPro("/nonexistent/song.gp", import, error));
}

TEST_CASE("an imported tab becomes a song: its chart and a backing of lahn's own, or its recording"){
    namespace fs = std::filesystem;
    GuitarProImport import;
    std::string error;
    REQUIRE_MESSAGE(readGpif(SCORE, import, error), error);

    // The backing: as long as the chart, with a moment after; not silent where the first note is
    const int rate = 22050;
    std::vector<float> backing = renderBacking(import.chart, rate);
    double seconds = tickToSeconds(import.chart, import.chart.endTick);
    CHECK(backing.size() >= (size_t)(seconds * rate));
    float first = 0.0f;
    for (int i = 0; i < rate / 10; i++) first = std::max(first, std::fabs(backing[i]));
    CHECK(first > 0.05f);
    float loudest = 0.0f;
    for (float sample : backing) loudest = std::max(loudest, std::fabs(sample));
    CHECK(loudest <= 0.9f + 1e-4f);

    fs::path songs = fs::temp_directory_path() / "lahn_tests" / "imported";
    fs::remove_all(songs);
    std::string chartPath;
    REQUIRE_MESSAGE(createImportedSong(songs.string(), import.chart, "", rate, chartPath, error), error);
    CHECK(fs::path(chartPath).parent_path().filename() == "Test Song");
    CHECK(fs::file_size(fs::path(chartPath).parent_path() / "backing.wav") == 44 + backing.size() * 2);
    Chart loaded;
    REQUIRE_MESSAGE(loadChart(chartPath, loaded, error), error);
    CHECK(loaded.audioFile == "backing.wav");
    CHECK(loaded.frettedTracks.size() == 2);
    CHECK(loaded.frettedTracks[0].notes.size() == import.chart.frettedTracks[0].notes.size());

    // The same song again, with a recording: a folder of its own beside the first, the audio copied in
    fs::path recording = fs::temp_directory_path() / "lahn_tests" / "Recording.MP3";
    { std::ofstream(recording) << "not really audio"; }
    std::string second;
    REQUIRE_MESSAGE(createImportedSong(songs.string(), import.chart, recording.string(), rate, second, error), error);
    CHECK(fs::path(second).parent_path().filename() == "Test Song 2");
    CHECK(fs::exists(fs::path(second).parent_path() / "audio.mp3"));
}

// Guitar Pro 6's compression, written the way it's read: bits, the high bit of each byte first
struct BitWriter {
    std::string bytes;
    int used = 8;
    void bit(int value){
        if (used == 8){ bytes += '\0'; used = 0; }
        if (value) bytes.back() = (char)(bytes.back() | (1 << (7 - used)));
        used++;
    }
    void write(int value, int count){ for (int i = count - 1; i >= 0; i--) bit((value >> i) & 1); }
    void writeReversed(int value, int count){ for (int i = 0; i < count; i++) bit((value >> i) & 1); }
};

static std::string bcfz(const std::string& unpacked, const std::vector<std::pair<int, int>>& copies = {}){
    std::string out = "BCFZ";
    for (int i = 0; i < 4; i++) out += (char)((unpacked.size() >> (8 * i)) & 0xFF);
    BitWriter bits;
    for (const auto& [back, size] : copies){ // copies first (the test's own data), then everything as it is
        bits.write(1, 1); bits.write(4, 4); bits.writeReversed(back, 4); bits.writeReversed(size, 4);
    }
    size_t at = 0;
    while (at < unpacked.size()){
        int size = (int)std::min<size_t>(3, unpacked.size() - at);
        bits.write(0, 1);
        bits.writeReversed(size, 2);
        for (int i = 0; i < size; i++) bits.write((unsigned char)unpacked[at + i], 8);
        at += size;
    }
    return out + bits.bytes;
}

TEST_CASE("Guitar Pro 6: its compression and its little file system"){
    std::string out, error;
    // Bytes as they are, then a copy of what's out already
    std::string stream = "BCFZ";
    stream += std::string("\x06\x00\x00\x00", 4); // six bytes unpacked
    BitWriter bits;
    bits.write(0, 1); bits.writeReversed(3, 2); bits.write('a', 8); bits.write('b', 8); bits.write('c', 8);
    bits.write(1, 1); bits.write(4, 4); bits.writeReversed(3, 4); bits.writeReversed(3, 4); // 3 back, 3 long
    REQUIRE_MESSAGE(unpackBcfz(stream + bits.bytes, out, error), error);
    CHECK(out == "abcabc");
    CHECK_FALSE(unpackBcfz("PK..", out, error));

    // A file system holding the score, packed: the same chart as the .gp
    const size_t SECTOR = 0x1000;
    std::string fileSystem(4 * SECTOR, '\0');
    auto put32 = [&](size_t at, int value){ for (int i = 0; i < 4; i++) fileSystem[at + i] = (char)((value >> (8 * i)) & 0xFF); };
    std::string xml = SCORE;
    REQUIRE(xml.size() < 2 * SECTOR);
    put32(SECTOR, 2);
    std::string name = "score.gpif";
    std::copy(name.begin(), name.end(), fileSystem.begin() + SECTOR + 4);
    put32(SECTOR + 0x8C, (int)xml.size());
    put32(SECTOR + 0x94, 2);
    put32(SECTOR + 0x98, 3);
    std::copy(xml.begin(), xml.end(), fileSystem.begin() + 2 * SECTOR);
    std::string packed = bcfz("BCFS" + fileSystem);
    std::string unpacked, score;
    REQUIRE_MESSAGE(unpackBcfz(packed, unpacked, error), error);
    REQUIRE(bcfsFile(unpacked, "score.gpif", score));
    CHECK(score == xml);
    CHECK_FALSE(bcfsFile(unpacked, "other.xml", score));

    namespace fs = std::filesystem;
    fs::path path = fs::temp_directory_path() / "lahn_tests" / "song.gpx";
    { std::ofstream(path, std::ios::binary) << packed; }
    GuitarProImport import;
    REQUIRE_MESSAGE(importGuitarPro(path.string(), import, error), error);
    CHECK(import.chart.title == "Test Song");
    CHECK(import.chart.frettedTracks.size() == 2);
}
