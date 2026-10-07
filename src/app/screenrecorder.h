#pragma once

#include <string>
#include <vector>

// What lahn shows, recorded as a video (a check of a song: F9 while playing). Its pictures, 30 a second, are taken as
// they're drawn and given to FFmpeg (the video add-on's: app/videoconvert), which makes them an mp4 (H.264, at most
// 720 lines high) as the song goes. The sound is put under it afterwards (addSoundToVideo).

bool startScreenRecording(const std::string& ffmpeg, const std::string& path, std::string& error);
bool screenRecording();
// Every frame, once it's drawn and before it's shown (before EndDrawing): a picture, when one is due
void captureScreen();
// The pictures given so far made into the file: it waits for FFmpeg to finish it
bool stopScreenRecording(std::string& error);

// A sound under a video: from `at` seconds into the video (negative: the sound's first seconds are cut), at `volume`
struct VideoSound {
    std::string path;
    double at = 0.0;
    float volume = 1.0f;
};
// The video's pictures as they are, with the sounds mixed under them, as `to`. Tens of seconds for a long video: for
// a worker thread.
bool addSoundToVideo(const std::string& ffmpeg, const std::string& video, const std::vector<VideoSound>& sounds,
                     const std::string& to, std::string& error);
