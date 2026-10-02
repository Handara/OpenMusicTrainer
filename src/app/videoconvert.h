#pragma once

#include <atomic>
#include <string>

// Videos of any kind (mp4, mkv, webm, mov...), made into what lahn plays. lahn reads one kind of video itself
// (MPEG-1: src/video) and a few kinds of audio; everything else goes through FFmpeg, a separate program that reads
// them all. It isn't part of lahn: it comes with the video add-on (core/addon; tools/make_video_addon.py), or is the
// one already installed on the system. A video is converted once, when it's brought into a song.
const char* const VIDEO_ADDON = "video";

// FFmpeg: the add-on's if it's installed, else the system's; "" if there's neither
std::string findFfmpeg(const std::string& addonsDir);

// Whether a file is a video, by its name: one lahn plays as it is (.mpg, .mpeg), or one for FFmpeg
bool isVideoFile(const std::string& path);
bool isPlayableVideo(const std::string& path);

// The video's pictures as a file lahn plays: MPEG-1, 480 lines high at most, 30 pictures a second, no sound.
// `progress` goes from 0 to 1 (null for none); `cancel` stops it. Nothing is left at `to` unless it worked.
// Minutes of video take tens of seconds: for a worker thread.
bool convertVideo(const std::string& ffmpeg, const std::string& from, const std::string& to, std::atomic<float>* progress,
                  const std::atomic<bool>& cancel, std::string& error);

// The video's sound as an mp3. False, saying so, for a video with none.
bool extractAudio(const std::string& ffmpeg, const std::string& from, const std::string& to, std::atomic<float>* progress,
                  const std::atomic<bool>& cancel, std::string& error);
