#pragma once

// Music theory helpers. Pitches use MIDI numbering: 60 = middle C (C4), 69 = A4 = 440 Hz,
// one step per semitone. A fractional MIDI pitch carries the cents: 69.25 is A4 + 25 cents.

const char* pitchClassName(int midiPitch); // "C", "C#", ... "B"
int pitchOctave(int midiPitch);            // 60 -> 4, 40 -> 2
float frequencyToMidi(float frequency);
float midiToFrequency(float midiPitch);
