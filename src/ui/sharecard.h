#pragma once

#include <string>

// A picture of the player's progress to share (the profile's S): a card over the screen, as wide as a link preview
// (1.91 to 1): their name, level, pp, streak, time practiced, notes known, their latest achievements and this week's
// practice. Enter saves it as a PNG in the shares folder (read back from the frame it's drawn in, so it's exactly
// what's shown), O opens that folder, Esc closes it.
void openShareCard();
void closeShareCard();
bool shareCardOpen();
// Over everything, while it's open; `name` the player's ("" for none chosen). Handles its keys.
void drawShareCard(const std::string& name, float scale);
// Right before the frame is shown: the card read back and saved, if Enter asked for it
void saveShareCardIfAsked(const std::string& sharesFolder);
