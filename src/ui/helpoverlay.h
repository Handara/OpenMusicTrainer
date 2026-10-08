#pragma once

// The controls, on one card over everything (F1, anywhere but the editors, which have their own): the keys, and the
// instrument's open strings that steer the menus. F1 or Esc closes it.
void toggleHelp();
bool helpOpen();
void closeHelp();
void drawHelpOverlay(float scale);
