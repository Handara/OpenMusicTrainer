#include "audio.h"

#include "miniaudio.h"

#include <chrono>
#include <cmath>

// songPosition() runs a smooth clock on the wall clock and pulls it toward the audio position each call.
// Each call closes this fraction of the gap: small enough to average out the audio position's ~10 ms steps.
const double DRIFT_CORRECTION = 0.05;
// A bigger gap means the audio really jumped (seek, stall, device change): snap instead of easing.
const double SNAP_THRESHOLD_S = 0.1;

// All audio state lives here, like raylib's internal AUDIO struct. miniaudio objects keep
// pointers to each other, so they must never move in memory: a single static instance guarantees it.
static struct {
    ma_engine engine;
    ma_sound song;
    bool engineReady = false;
    bool songReady = false;

    ma_uint32 songSampleRate = 0;
    double songLengthS = 0.0;
    bool looping = false;

    // Clock smoothing: the audio position only changes when the audio thread processes a chunk,
    // so on its own it moves in steps. smoothTime advances continuously and follows it.
    // When looping, smoothTime keeps counting past the loop point; songPosition() wraps it on return.
    double smoothTime = 0.0;
    double lastWallTime = 0.0;
} audio;

static double wallClockSeconds(){
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

bool initAudio(std::string& error){
    ma_result result = ma_engine_init(nullptr, &audio.engine); // default output device and settings
    if (result != MA_SUCCESS){
        error = std::string("could not start audio engine: ") + ma_result_description(result);
        return false;
    }
    audio.engineReady = true;
    return true;
}

void closeAudio(){
    unloadSong();
    if (audio.engineReady) ma_engine_uninit(&audio.engine);
    audio.engineReady = false;
}

const char* audioBackendName(){
    if (!audio.engineReady) return "none";
    ma_device* device = ma_engine_get_device(&audio.engine);
    return ma_get_backend_name(device->pContext->backend);
}

bool loadSong(const std::string& path, std::string& error){
    unloadSong();
    // STREAM decodes a little at a time instead of the whole file up front
    // (a 5 minute song fully decoded is ~100 MB). NO_SPATIALIZATION skips unneeded 3D audio processing.
    ma_uint32 flags = MA_SOUND_FLAG_STREAM | MA_SOUND_FLAG_NO_SPATIALIZATION;
    ma_result result = ma_sound_init_from_file(&audio.engine, path.c_str(), flags, nullptr, nullptr, &audio.song);
    if (result != MA_SUCCESS){
        error = path + ": could not load audio: " + ma_result_description(result);
        return false;
    }
    ma_sound_get_data_format(&audio.song, nullptr, nullptr, &audio.songSampleRate, nullptr, 0);
    ma_uint64 lengthFrames = 0;
    ma_sound_get_length_in_pcm_frames(&audio.song, &lengthFrames);
    audio.songLengthS = (double)lengthFrames / audio.songSampleRate;
    audio.songReady = true;
    return true;
}

void unloadSong(){
    if (audio.songReady) ma_sound_uninit(&audio.song);
    audio.songReady = false;
}

void playSong(bool loop){
    if (!audio.songReady) return;
    audio.looping = loop;
    ma_sound_set_looping(&audio.song, loop ? MA_TRUE : MA_FALSE);
    ma_sound_seek_to_pcm_frame(&audio.song, 0);
    audio.smoothTime = 0.0;
    audio.lastWallTime = wallClockSeconds();
    ma_sound_start(&audio.song);
}

double songLength(){
    return audio.songReady ? audio.songLengthS : 0.0;
}

double songPosition(){
    if (!audio.songReady) return 0.0;

    ma_uint64 cursor = 0;
    ma_sound_get_cursor_in_pcm_frames(&audio.song, &cursor);
    double audioTime = (double)cursor / audio.songSampleRate;
    double now = wallClockSeconds();
    double length = audio.songLengthS;

    if (!ma_sound_is_playing(&audio.song)){
        audio.smoothTime = audioTime;
        audio.lastWallTime = now;
        return audioTime;
    }

    audio.smoothTime += now - audio.lastWallTime;
    audio.lastWallTime = now;

    // Where the smooth clock is inside the song, and how far the audio is from it
    double wrapped = audio.looping ? std::fmod(audio.smoothTime, length) : audio.smoothTime;
    double drift = audioTime - wrapped;
    if (audio.looping){
        // Around the loop point one side has wrapped and the other hasn't yet: 8.99 vs 0.01 is 0.02 apart, not 8.98
        if (drift > length / 2) drift -= length;
        if (drift < -length / 2) drift += length;
    }

    if (std::fabs(drift) > SNAP_THRESHOLD_S) audio.smoothTime += drift;
    else audio.smoothTime += drift * DRIFT_CORRECTION;

    return audio.looping ? std::fmod(audio.smoothTime, length) : audio.smoothTime;
}
