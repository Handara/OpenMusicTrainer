#pragma once

#include <cstdint>
#include <vector>

// MIDI, the language keyboards, pads and controllers speak: decoding the bytes a device sends into what was
// played. Pure logic; reading them from the device is input/midi's job.
//
// A message is a status byte (the top bit set: what it is, and on which of 16 channels) and one or two data bytes.
// Devices may leave the status out when it repeats ("running status"), send a note on at velocity 0 for a note off,
// drop one-byte real-time messages (the clock) anywhere, even inside another message, and send system exclusive
// blocks of any length. The parser handles all of that.

struct MidiEvent {
    enum class Kind { NoteOn, NoteOff, ControlChange };
    Kind kind;
    int channel;   // 0 to 15
    int number;    // the note's pitch (60 = middle C), or the controller's number (64 = the sustain pedal)
    int value;     // the note's velocity (1 to 127), or the controller's value
};

struct MidiParser {
    uint8_t status = 0;       // the running status: the last channel message's status byte, 0 for none
    uint8_t data[2] = {};
    int have = 0;             // data bytes of the current message so far
    bool inSysex = false;     // inside a system exclusive block: everything is skipped until its end
    int skipCommon = 0;       // data bytes of a system common message still to skip
};

// Decodes more bytes, appending the notes and controller changes they finish
void feedMidi(MidiParser& parser, const uint8_t* bytes, int count, std::vector<MidiEvent>& out);
