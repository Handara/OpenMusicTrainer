#pragma once

// Hearing the instrument as a clean synth bass, wherever the player is in the game: what the monitor gathers from the
// instrument's inputs (audio.h: setMonitor, in synth mode) goes through a note detector of its own, and each note it
// finds plays on the synth bass, one at a time like the instrument; muting the string fades it. Nothing raw is heard,
// so no hum, no noise, no crackle, and every note heard is a note the game understood. It's later than the raw
// sound by the time a pitch takes to find (see core/notedetector).

// Once a frame, from the main loop. `on`: the settings want it; `volume`: 0..1
void updateSynthMonitor(bool on, float volume);

// In a song, the note due now (-1 for none), each frame before updateSynthMonitor: a pluck then plays it at once,
// at the attack, instead of waiting the time a pitch takes to find. If what was played turns out to be another note,
// that one takes over as soon as it's known.
void expectSynthNote(int pitch);
