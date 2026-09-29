#include "doctest/doctest.h"

#include "core/midi.h"

static std::vector<MidiEvent> decode(MidiParser& parser, std::vector<uint8_t> bytes){
    std::vector<MidiEvent> events;
    feedMidi(parser, bytes.data(), (int)bytes.size(), events);
    return events;
}

TEST_CASE("notes on and off"){
    MidiParser parser;
    std::vector<MidiEvent> events = decode(parser, { 0x90, 60, 100, 0x80, 60, 40 });
    REQUIRE(events.size() == 2);
    CHECK(events[0].kind == MidiEvent::Kind::NoteOn);
    CHECK(events[0].number == 60);
    CHECK(events[0].value == 100);
    CHECK(events[1].kind == MidiEvent::Kind::NoteOff);
    CHECK(events[1].number == 60);
}

TEST_CASE("running status, and a note on at velocity 0 is a note off"){
    MidiParser parser;
    // One status byte, then three notes and their releases as velocity 0, as many keyboards send them
    std::vector<MidiEvent> events = decode(parser, { 0x93, 60, 90, 64, 91, 67, 92, 60, 0, 64, 0 });
    REQUIRE(events.size() == 5);
    CHECK(events[2].number == 67);
    CHECK(events[2].channel == 3);
    CHECK(events[3].kind == MidiEvent::Kind::NoteOff);
    CHECK(events[4].kind == MidiEvent::Kind::NoteOff);
    CHECK(events[4].number == 64);
}

TEST_CASE("a message split across reads"){
    MidiParser parser;
    CHECK(decode(parser, { 0x90 }).empty());
    CHECK(decode(parser, { 62 }).empty());
    std::vector<MidiEvent> events = decode(parser, { 70 });
    REQUIRE(events.size() == 1);
    CHECK(events[0].number == 62);
}

TEST_CASE("real time bytes inside a message, and system exclusive blocks, are skipped"){
    MidiParser parser;
    std::vector<MidiEvent> events = decode(parser, { 0x90, 0xF8, 60, 0xFE, 100,        // clock and active sensing mid-note
                                                     0xF0, 0x43, 0x10, 0x4C, 0x00, 0xF7, // a Yamaha sysex block
                                                     0x90, 61, 100 });
    REQUIRE(events.size() == 2);
    CHECK(events[0].number == 60);
    CHECK(events[1].number == 61);
}

TEST_CASE("the sustain pedal, and messages the game doesn't use"){
    MidiParser parser;
    std::vector<MidiEvent> events = decode(parser, { 0xB0, 64, 127,   // sustain down
                                                     0xE0, 0x00, 0x40, // pitch bend: ignored, but its bytes are consumed
                                                     0xC0, 5,          // program change: one data byte
                                                     0x90, 48, 80 });
    REQUIRE(events.size() == 2);
    CHECK(events[0].kind == MidiEvent::Kind::ControlChange);
    CHECK(events[0].number == 64);
    CHECK(events[0].value == 127);
    CHECK(events[1].kind == MidiEvent::Kind::NoteOn);
    CHECK(events[1].number == 48);
}

TEST_CASE("system common messages end running status and are skipped"){
    MidiParser parser;
    std::vector<MidiEvent> events = decode(parser, { 0x90, 60, 100, 0xF2, 0x10, 0x20, 62, 100 }); // song position, then stray data
    REQUIRE(events.size() == 1); // the data after it has no status any more
    CHECK(events[0].number == 60);
}
