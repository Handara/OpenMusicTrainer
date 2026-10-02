#pragma once

#include "raylib.h"

#include <string>

// Video: MPEG-1 files (.mpg), decoded by pl_mpeg. For lessons, one video at a time with its own sound (MP2), like a
// song; and for songs, a video's pictures alone, behind the notes (further down).
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

// A song's video: its pictures only, shown behind the notes. It has no clock of its own: whoever plays the song says
// where the song is, each frame, and gets the picture for that moment. So it follows the song through a pause, a
// restart or a jump, and can't drift from it. Its own file, apart from a lesson's video: both can be open.
bool openSongVideo(const std::string& path, std::string& error); // reads the whole file into memory
void closeSongVideo();                                           // safe when none is open
bool songVideoOpen();
// The picture `seconds` into the video: before its start, its first; past its end, its last
const Texture2D& songVideoTexture(double seconds);
