#pragma once

#include "core/settings.h"

// The Instrument screen: your instrument, drawn, lighting up as you play it.
// Guitar and bass: a note played lights up on the neck where it was most likely played (from where the hand last
// was: core/positions), the other places the same note lives shown faintly, and the last few notes fading behind
// it, so a scale shows its shape. Each listens on the input the Instruments settings gave it.
// Piano: the keys held down light up and sound, from a MIDI keyboard or the computer keys.

void openInstrumentScreen(const Settings& settings);
void closeInstrumentScreen(); // stops listening; safe to call more than once
void instrumentScreen();      // draws and listens, once per frame; the caller leaves on Esc
