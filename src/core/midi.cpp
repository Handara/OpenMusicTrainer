#include "core/midi.h"

// How many data bytes a channel message has, by its kind (the status byte's top four bits)
static int dataBytesFor(uint8_t status){
    switch (status & 0xF0){
        case 0xC0: case 0xD0: return 1; // program change, channel pressure
        default: return 2;              // note off and on, key pressure, control change, pitch bend
    }
}

static void finish(MidiParser& parser, std::vector<MidiEvent>& out){
    uint8_t kind = parser.status & 0xF0;
    int channel = parser.status & 0x0F;
    if (kind == 0x90 && parser.data[1] > 0) out.push_back({MidiEvent::Kind::NoteOn, channel, parser.data[0], parser.data[1]});
    else if (kind == 0x80 || kind == 0x90) out.push_back({MidiEvent::Kind::NoteOff, channel, parser.data[0], parser.data[1]});
    else if (kind == 0xB0) out.push_back({MidiEvent::Kind::ControlChange, channel, parser.data[0], parser.data[1]});
    // the rest (pressure, program change, pitch bend) isn't used by the game
}

void feedMidi(MidiParser& parser, const uint8_t* bytes, int count, std::vector<MidiEvent>& out){
    for (int i = 0; i < count; i++){
        uint8_t byte = bytes[i];
        if (byte >= 0xF8) continue;                // real time (clock, start, stop...): one byte, anywhere, ignored
        if (byte == 0xF0){ parser.inSysex = true; continue; }
        if (byte == 0xF7){ parser.inSysex = false; continue; }
        if (parser.inSysex){
            if (byte & 0x80) parser.inSysex = false; // a status byte ends a block that lost its end byte
            else continue;
        }
        if (byte >= 0xF0){
            // System common: song position (2 data bytes), song select and time code (1), tune request (0).
            // It ends running status.
            parser.status = 0;
            parser.skipCommon = byte == 0xF2 ? 2 : (byte == 0xF1 || byte == 0xF3) ? 1 : 0;
            continue;
        }
        if (byte & 0x80){                          // a channel message's status: a new message starts
            parser.status = byte;
            parser.have = 0;
            parser.skipCommon = 0;
            continue;
        }
        if (parser.skipCommon > 0){ parser.skipCommon--; continue; }
        if (parser.status == 0) continue;          // data with no status to belong to: dropped
        parser.data[parser.have++] = byte;
        if (parser.have == dataBytesFor(parser.status)){
            finish(parser, out);
            parser.have = 0;                       // running status: the next data bytes are another message
        }
    }
}
