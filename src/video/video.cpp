#include "video/video.h"

#include "audio/audio.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <vector>

#include "pl_mpeg.h" // after the standard headers: it uses size_t and FILE without including them

// Two decoders read the same file from memory: one for pictures (the main thread), one for sound (the audio thread,
// through the song reader). Each only ever touches its own, so they need no locking. The file is in memory so the
// audio thread never waits on the disk.
static struct {
    std::string path;
    std::vector<uint8_t> file;
    plm_t* pictures = nullptr;
    plm_t* sound = nullptr;
    bool hasSound = false;

    Texture2D texture{};
    std::vector<uint8_t> rgba;   // the newest due picture, converted, waiting to be uploaded
    plm_frame_t* pending = nullptr; // decoded but not due yet (valid until the next decode)
    bool uploadNeeded = false;
    bool firstShown = false;     // before playing, the first picture is shown (and only it)
    bool finished = false;       // no more pictures

    // The sound decoder hands out 1152 samples at a time; what the audio device didn't take yet waits here
    plm_samples_t* samples = nullptr;
    int samplesUsed = 0;

    bool playing = false;
    double startTime = 0.0;      // audio engine time when a silent video started
} video;

// Called by the audio thread for the video's sound: copies decoded stereo samples, decoding more as needed
static int readSound(void*, float* out, int frames){
    int written = 0;
    while (written < frames){
        if (!video.samples || video.samplesUsed == (int)video.samples->count){
            video.samples = plm_decode_audio(video.sound);
            video.samplesUsed = 0;
            if (!video.samples) break; // the end
        }
        int count = std::min(frames - written, (int)video.samples->count - video.samplesUsed);
        const float* from = video.samples->interleaved + video.samplesUsed * 2;
        std::copy(from, from + count * 2, out + written * 2);
        written += count;
        video.samplesUsed += count;
    }
    return written;
}

void closeVideo(){
    if (video.path.empty()) return;
    if (video.hasSound) unloadSong(); // first: the audio thread stops reading before the decoder goes
    plm_destroy(video.pictures);
    if (video.sound) plm_destroy(video.sound);
    UnloadTexture(video.texture);
    video = {};
}

bool openVideo(const std::string& path, std::string& error){
    closeVideo();
    int size = 0;
    unsigned char* data = LoadFileData(path.c_str(), &size);
    if (!data){
        error = "Could not read " + path;
        return false;
    }
    video.file.assign(data, data + size);
    UnloadFileData(data);

    video.pictures = plm_create_with_memory(video.file.data(), video.file.size(), 0);
    if (!video.pictures || !plm_probe(video.pictures, 5000 * 1024) || plm_get_num_video_streams(video.pictures) == 0){
        if (video.pictures) plm_destroy(video.pictures);
        video = {};
        error = "Not an MPEG-1 video: convert it to .mpg (the lesson editor shows how)";
        return false;
    }
    plm_set_audio_enabled(video.pictures, 0);
    plm_set_loop(video.pictures, 0);

    Image black = GenImageColor(plm_get_width(video.pictures), plm_get_height(video.pictures), BLACK);
    video.texture = LoadTextureFromImage(black);
    UnloadImage(black);
    SetTextureFilter(video.texture, TEXTURE_FILTER_BILINEAR);
    // pl_mpeg writes red, green and blue but leaves alpha alone: start opaque, and it stays opaque
    video.rgba.assign((size_t)video.texture.width * video.texture.height * 4, 255);
    video.path = path;

    video.hasSound = plm_get_num_audio_streams(video.pictures) > 0;
    if (video.hasSound){
        video.sound = plm_create_with_memory(video.file.data(), video.file.size(), 0);
        plm_set_video_enabled(video.sound, 0);
        plm_set_loop(video.sound, 0);
        if (!loadSongFromReader(readSound, nullptr, plm_get_samplerate(video.sound), 2, plm_get_duration(video.sound), error)){
            closeVideo();
            return false;
        }
    }
    return true;
}

bool videoOpen(const std::string& path){
    return !video.path.empty() && video.path == path;
}

void playVideo(){
    if (video.path.empty()) return;
    if (video.playing){
        // Again from the start: both decoders rewind together
        plm_rewind(video.pictures);
        if (video.hasSound){
            std::string error;
            unloadSong();
            plm_rewind(video.sound);
            video.samples = nullptr;
            loadSongFromReader(readSound, nullptr, plm_get_samplerate(video.sound), 2, plm_get_duration(video.sound), error);
        }
        video.pending = nullptr;
        video.finished = false;
    }
    if (video.hasSound) playSong(false);
    video.startTime = audioTime();
    video.playing = true;
}

bool videoPlaying(){
    if (!video.playing) return false;
    return video.hasSound ? !songEnded() : !video.finished;
}

double videoPosition(){
    if (!video.playing) return 0.0;
    return video.hasSound ? songPosition() : audioTime() - video.startTime;
}

double videoLength(){
    return video.path.empty() ? 0.0 : plm_get_duration(video.pictures);
}

// Decodes the next picture, or keeps the one already decoded but not yet due; nullptr at the end
static plm_frame_t* nextPicture(){
    if (!video.pending) video.pending = plm_decode_video(video.pictures);
    if (!video.pending) video.finished = true;
    return video.pending;
}

static void takePicture(plm_frame_t* picture){
    plm_frame_to_rgba(picture, video.rgba.data(), video.texture.width * 4);
    video.uploadNeeded = true;
    video.pending = nullptr; // its memory is reused by the next decode
}

const Texture2D& videoTexture(){
    if (video.path.empty()) return video.texture;
    if (!video.playing){
        // Before playing: the first picture, as a poster
        if (!video.firstShown && nextPicture()) takePicture(video.pending);
        video.firstShown = true;
    } else {
        // Every picture due by now, in order; converting each keeps the code simple, and it's rarely more than one
        double now = videoPosition();
        while (!video.finished && nextPicture() && video.pending->time <= now) takePicture(video.pending);
    }
    if (video.uploadNeeded){
        UpdateTexture(video.texture, video.rgba.data());
        video.uploadNeeded = false;
    }
    return video.texture;
}

// --- A song's video -----------------------------------------------------------------------------------

const double SEEK_BEHIND_S = 0.05; // asked for a moment this far before the picture shown: the song went back
const double SEEK_AHEAD_S = 1.0;   // or this far past it: the song jumped ahead. Otherwise pictures are decoded in order.

static struct {
    std::vector<uint8_t> file;
    plm_t* pictures = nullptr;
    Texture2D texture{};
    std::vector<uint8_t> rgba;
    plm_frame_t* pending = nullptr; // decoded, not due yet
    double shownTime = -1.0;        // the time of the picture in the texture; -1 before the first
    bool finished = false;
} backdrop;

void closeSongVideo(){
    if (!backdrop.pictures) return;
    plm_destroy(backdrop.pictures);
    UnloadTexture(backdrop.texture);
    backdrop = {};
}

bool songVideoOpen(){
    return backdrop.pictures != nullptr;
}

bool openSongVideo(const std::string& path, std::string& error){
    closeSongVideo();
    int size = 0;
    unsigned char* data = LoadFileData(path.c_str(), &size);
    if (!data){
        error = "Could not read " + path;
        return false;
    }
    backdrop.file.assign(data, data + size);
    UnloadFileData(data);
    backdrop.pictures = plm_create_with_memory(backdrop.file.data(), backdrop.file.size(), 0);
    if (!backdrop.pictures || !plm_probe(backdrop.pictures, 5000 * 1024) || plm_get_num_video_streams(backdrop.pictures) == 0){
        if (backdrop.pictures) plm_destroy(backdrop.pictures);
        backdrop = {};
        error = "Not an MPEG-1 video";
        return false;
    }
    plm_set_audio_enabled(backdrop.pictures, 0);
    plm_set_loop(backdrop.pictures, 0);
    Image black = GenImageColor(plm_get_width(backdrop.pictures), plm_get_height(backdrop.pictures), BLACK);
    backdrop.texture = LoadTextureFromImage(black);
    UnloadImage(black);
    SetTextureFilter(backdrop.texture, TEXTURE_FILTER_BILINEAR);
    backdrop.rgba.assign((size_t)backdrop.texture.width * backdrop.texture.height * 4, 255); // opaque: pl_mpeg leaves alpha alone
    return true;
}

static void showPicture(plm_frame_t* picture){
    plm_frame_to_rgba(picture, backdrop.rgba.data(), backdrop.texture.width * 4);
    backdrop.shownTime = picture->time;
}

const Texture2D& songVideoTexture(double seconds){
    if (!backdrop.pictures) return backdrop.texture;
    seconds = std::max(0.0, seconds);
    const double before = backdrop.shownTime;
    // The song went back, or jumped ahead: to the nearest picture the video can start from, then on from there
    const bool pastTheEnd = backdrop.finished && seconds > backdrop.shownTime;
    if (backdrop.shownTime >= 0.0 && !pastTheEnd && (seconds < backdrop.shownTime - SEEK_BEHIND_S || seconds > backdrop.shownTime + SEEK_AHEAD_S)){
        backdrop.pending = nullptr;
        backdrop.finished = false;
        if (plm_frame_t* found = plm_seek_frame(backdrop.pictures, seconds, 0)) showPicture(found);
    }
    // Every picture due by now, in order (the first one always: there's nothing to show before it). Converting each
    // keeps the code simple, and it's rarely more than one.
    while (!backdrop.finished){
        if (!backdrop.pending) backdrop.pending = plm_decode_video(backdrop.pictures);
        if (!backdrop.pending){
            backdrop.finished = true;
            break;
        }
        if (backdrop.shownTime >= 0.0 && backdrop.pending->time > seconds) break; // not due yet
        showPicture(backdrop.pending);
        backdrop.pending = nullptr; // its memory is reused by the next decode
    }
    if (backdrop.shownTime != before) UpdateTexture(backdrop.texture, backdrop.rgba.data());
    return backdrop.texture;
}
