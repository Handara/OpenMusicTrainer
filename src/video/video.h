#pragma once

#include "raylib.h"

#include <string>

// Video for lessons: MPEG-1 files (.mpg, with MP2 sound), decoded by pl_mpeg. One video at a time, like songs.
//
// Its sound plays as the song (audio module), and its pictures follow the song's clock: each frame, the pictures
// due by the song's position are decoded and the newest is shown. Picture and sound can't drift apart, the same
// way notes can't drift from a song. A video without sound follows the audio engine's clock instead.

bool openVideo(const std::string& path, std::string& error); // reads the whole file into memory
void closeVideo();                                           // stops it, and its sound
bool videoOpen(const std::string& path);                     // is this file the one open?

void playVideo();   // from the start
bool videoPlaying();
double videoPosition();
double videoLength();

// The picture for now: decodes what's due and returns the texture to draw (before playing: the first picture)
const Texture2D& videoTexture();
