#include "audio/audio.h"

#include "core/synth.h"
#include "miniaudio.h"

#include <chrono>
#include <cmath>
#include <cstring>
#include <vector>

// songPosition() runs a smooth clock on the wall clock and pulls it toward the audio position each call.
// Each call closes this fraction of the gap: small enough to average out the audio position's ~10 ms steps.
const double DRIFT_CORRECTION = 0.05;
// A bigger gap means the audio really jumped (seek, stall, device change): snap instead of easing.
const double SNAP_THRESHOLD_S = 0.1;

// Captured audio waiting for the main thread. At 48 kHz this is ~0.34 s: room for several slow frames.
const ma_uint32 CAPTURE_BUFFER_FRAMES = 16384;

// Preview sounds (e.g. the editor playing a note you place). Several can ring at once, like real strings.
const int VOICE_COUNT = 8;
const float PLUCK_LENGTH_S = 1.5f;
const float PLUCK_VOLUME = 0.6f;

// One preview sound: its samples, and the miniaudio objects playing them.
// ma_audio_buffer reads straight from `samples` (no copy), so they must stay alive while it plays.
struct Voice {
    std::vector<float> samples;
    ma_audio_buffer buffer;
    ma_sound sound;
    bool ready = false;
    unsigned long long startedAt = 0; // which pluck this was, to find the oldest when all voices are busy
};

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

    // Input (microphone / instrument). The audio thread writes into captureBuffer, the main thread reads from it.
    ma_device captureDevice;
    ma_pcm_rb captureBuffer;
    bool captureReady = false;

    Voice voices[VOICE_COUNT];
    unsigned long long pluckCount = 0;
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

static void releaseVoice(Voice& voice){
    if (!voice.ready) return;
    ma_sound_uninit(&voice.sound); // detaches from the engine first, so the audio thread stops reading the buffer
    ma_audio_buffer_uninit(&voice.buffer);
    voice.ready = false;
}

void closeAudio(){
    for (Voice& voice : audio.voices) releaseVoice(voice);
    stopCapture();
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

bool songEnded(){
    return audio.songReady && ma_sound_at_end(&audio.song);
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

// Runs on the audio thread, every few milliseconds, with fresh input samples.
// Rules for this function: no allocation, no locks, no file or console I/O. Anything slow here makes
// the audio device miss its deadline and drop samples. So: copy into the ring buffer and return.
static void captureCallback(ma_device* device, void* output, const void* input, ma_uint32 frameCount){
    (void)device;
    (void)output;
    const float* samples = (const float*)input;
    ma_uint32 written = 0;
    while (written < frameCount){
        // The free space may wrap around the end of the ring, so it can come in two pieces: loop
        ma_uint32 chunk = frameCount - written;
        void* destination;
        if (ma_pcm_rb_acquire_write(&audio.captureBuffer, &chunk, &destination) != MA_SUCCESS || chunk == 0) break;
        memcpy(destination, samples + written, chunk * sizeof(float));
        ma_pcm_rb_commit_write(&audio.captureBuffer, chunk);
        written += chunk;
    }
    // If the buffer was full, the rest is dropped: the main thread stopped reading, old audio is useless anyway
}

bool startCapture(std::string& error){
    stopCapture();

    // miniaudio's ring buffer is lock-free for exactly one writer thread and one reader thread
    ma_result result = ma_pcm_rb_init(ma_format_f32, 1, CAPTURE_BUFFER_FRAMES, nullptr, nullptr, &audio.captureBuffer);
    if (result != MA_SUCCESS){
        error = std::string("could not create capture buffer: ") + ma_result_description(result);
        return false;
    }

    ma_device_config config = ma_device_config_init(ma_device_type_capture);
    config.capture.format = ma_format_f32;
    config.capture.channels = 1; // pitch detection needs one channel; miniaudio mixes stereo inputs down
    config.sampleRate = 0;       // the device's native rate, so nothing gets resampled
    config.dataCallback = captureCallback;
    result = ma_device_init(nullptr, &config, &audio.captureDevice);
    if (result != MA_SUCCESS){
        ma_pcm_rb_uninit(&audio.captureBuffer);
        error = std::string("could not open input device: ") + ma_result_description(result);
        return false;
    }
    result = ma_device_start(&audio.captureDevice);
    if (result != MA_SUCCESS){
        ma_device_uninit(&audio.captureDevice);
        ma_pcm_rb_uninit(&audio.captureBuffer);
        error = std::string("could not start input device: ") + ma_result_description(result);
        return false;
    }
    audio.captureReady = true;
    return true;
}

void stopCapture(){
    if (!audio.captureReady) return;
    ma_device_uninit(&audio.captureDevice); // stops the audio thread before the buffer it writes to goes away
    ma_pcm_rb_uninit(&audio.captureBuffer);
    audio.captureReady = false;
}

int captureSampleRate(){
    return audio.captureReady ? (int)audio.captureDevice.sampleRate : 0;
}

const char* captureDeviceName(){
    return audio.captureReady ? audio.captureDevice.capture.name : "none";
}

int readCapture(float* out, int maxFrames){
    if (!audio.captureReady) return 0;
    int total = 0;
    while (total < maxFrames){
        ma_uint32 chunk = (ma_uint32)(maxFrames - total);
        void* source;
        if (ma_pcm_rb_acquire_read(&audio.captureBuffer, &chunk, &source) != MA_SUCCESS || chunk == 0) break;
        memcpy(out + total, source, chunk * sizeof(float));
        ma_pcm_rb_commit_read(&audio.captureBuffer, chunk);
        total += (int)chunk;
    }
    return total;
}

void playPluck(float frequency){
    if (!audio.engineReady) return;

    // A voice that finished playing, or else the oldest one (it has faded the most)
    Voice* voice = &audio.voices[0];
    for (Voice& candidate : audio.voices){
        if (!candidate.ready || !ma_sound_is_playing(&candidate.sound)){
            voice = &candidate;
            break;
        }
        if (candidate.startedAt < voice->startedAt) voice = &candidate;
    }
    releaseVoice(*voice);

    // Rendered at the engine's own sample rate, so nothing has to be resampled while it plays
    ma_uint32 sampleRate = ma_engine_get_sample_rate(&audio.engine);
    voice->samples.resize((size_t)(PLUCK_LENGTH_S * sampleRate)); // keeps its memory between plucks: no allocation after the first
    renderPluck(voice->samples.data(), (int)voice->samples.size(), frequency, (int)sampleRate, (unsigned)audio.pluckCount);

    ma_audio_buffer_config config = ma_audio_buffer_config_init(ma_format_f32, 1, voice->samples.size(), voice->samples.data(), nullptr);
    config.sampleRate = sampleRate;
    if (ma_audio_buffer_init(&config, &voice->buffer) != MA_SUCCESS) return;
    if (ma_sound_init_from_data_source(&audio.engine, &voice->buffer, MA_SOUND_FLAG_NO_SPATIALIZATION, nullptr, &voice->sound) != MA_SUCCESS){
        ma_audio_buffer_uninit(&voice->buffer);
        return;
    }
    voice->ready = true;
    voice->startedAt = ++audio.pluckCount;
    ma_sound_set_volume(&voice->sound, PLUCK_VOLUME);
    ma_sound_start(&voice->sound);
}
