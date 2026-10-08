#pragma once

#include <string>

// The game recorded as a video, from any screen: F10 starts recording what's shown and what's heard (lahn's own sound,
// as it went out: the song, the clicks, the instrument through its tone), F10 again saves it as an mp4 in the data
// folder's "recordings". F12 saves the screen there as a picture. The video needs FFmpeg (the video add-on's, or the
// system's); while it records, a small REC shows, which isn't in the video.

void initGameRecorder(const std::string& userDataDir);
// Each frame: the sound kept so far, written down
void updateGameRecorder();
// In the UI's frame: F10 and F12 (read as every key the menus read), and what was just saved, with its folder to open
// (never over a recording, nor in a screenshot)
void gameRecorderUi(float scale);
// Once the frame is drawn, before anything's over it: the screenshot asked for
void captureGameRecorder();
// Last, after every picture is taken: REC and how long, kept out of the video
void drawGameRecorderOverlay(float scale);
// On the way out: a recording going is saved (this waits for it)
void closeGameRecorder();
