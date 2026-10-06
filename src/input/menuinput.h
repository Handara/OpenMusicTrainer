#pragma once

#include "core/settings.h"

// The instrument as a controller in the menus (core/menunotes): while a menu screen is up, the instrument played is
// listened to; its open strings press the arrows, Enter and Esc for the menus (as keys do), and on a short menu a
// note picks an item. Call once a frame, before the UI's frame starts: the keys it presses count in that frame.
//
// It shares the input with the screens that listen: it starts note input only while a menu is up, and leaving the
// menus it closes it only if nobody else started listening since (an exercise, the tuning check).
void updateMenuInput(bool listen, const Settings& settings, InputRole instrument);
bool menuInputActive();          // listening now: the menus show the notes
bool menuInputBass();            // listening to a bass
bool menuInputBack();            // the note for back was played this frame (Esc's job: the app's own back)
int menuInputItem();             // an item's note played this frame: which (from 0), else -1
int menuInputHeard(double& at);  // the control heard last (0 back, 1 up, 2 down, 3 choose, 4 left, 5 right; -1 none), when

// The little fretboard showing which string does what, at the bottom right of a menu screen
void drawMenuInputLegend(float scale);
