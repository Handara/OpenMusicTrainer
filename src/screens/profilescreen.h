#pragma once

#include "core/settings.h"

// The player's profile (core/profile, kept by app/playerprogress): their level, its title and the XP to the next;
// what they've done (time practiced, notes right, accuracy, streaks); a calendar of the last weeks' practice against
// the daily goal; the notes they miss most lately; and every achievement, its medal lit once earned (the date) or
// dim with how far along it is. Left and Right change the daily goal.

void openProfileScreen();
// True when the daily goal changed (the settings are to be saved)
bool profileScreen(Settings& settings);
