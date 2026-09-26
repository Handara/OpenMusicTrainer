#include "audio/audio.h"

#include "core/pitch.h"
#include "core/settings.h"
#include "core/synth.h"
#include "miniaudio.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
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
const float PREVIEW_LENGTH_S = 1.5f;     // built-in sounds
const float MAX_CUSTOM_SOUND_S = 3.0f;   // longer sound files are cut (and faded) here
const char* const SOUND_FILE_EXTENSIONS[] = { ".wav", ".mp3", ".flac" }; // what miniaudio decodes out of the box
const float SILENCE_LEVEL = 0.001f;      // -60 dB: quieter than this at the start of a sound file counts as silence

// One preview sound: its samples, and the miniaudio objects playing them.
// ma_audio_buffer reads straight from the samples (no copy), so they must stay alive while it plays:
// built-in sounds are rendered into the voice's own `samples`, custom sounds are read from `customSound`.
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
    ma_context context; // the connection to the system's audio (WASAPI, PulseAudio...): lists and opens devices
    bool contextReady = false;
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
    unsigned long long previewCount = 0;
    float previewVolume = 0.6f;
    std::string previewSoundName = "pluck";
    std::string soundsDir;
    std::vector<float> customSound;   // the decoded file, at the engine's sample rate; empty for built-in sounds
    float customSoundRoot = 0.0f;     // its detected pitch in Hz, 0 if it has none (then it never gets re-pitched)
} audio;

static double wallClockSeconds(){
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

// Finds a device by the name the system gives it. Names are what the settings file stores:
// device ids are opaque, backend-specific data, but a name is readable and stays the same between runs.
static bool findDevice(ma_device_type type, const std::string& name, ma_device_id& id){
    if (name.empty() || !audio.contextReady) return false;
    ma_device_info* playback; ma_uint32 playbackCount;
    ma_device_info* capture; ma_uint32 captureCount;
    if (ma_context_get_devices(&audio.context, &playback, &playbackCount, &capture, &captureCount) != MA_SUCCESS) return false;
    ma_device_info* devices = type == ma_device_type_playback ? playback : capture;
    ma_uint32 count = type == ma_device_type_playback ? playbackCount : captureCount;
    for (ma_uint32 i = 0; i < count; i++){
        if (name == devices[i].name){
            id = devices[i].id;
            return true;
        }
    }
    return false;
}

static std::vector<std::string> deviceNames(ma_device_type type){
    std::vector<std::string> names;
    ma_device_info* playback; ma_uint32 playbackCount;
    ma_device_info* capture; ma_uint32 captureCount;
    if (!audio.contextReady || ma_context_get_devices(&audio.context, &playback, &playbackCount, &capture, &captureCount) != MA_SUCCESS){
        return names;
    }
    ma_device_info* devices = type == ma_device_type_playback ? playback : capture;
    ma_uint32 count = type == ma_device_type_playback ? playbackCount : captureCount;
    for (ma_uint32 i = 0; i < count; i++) names.push_back(devices[i].name); // copied: the list is only valid until the next query
    return names;
}

std::vector<std::string> outputDeviceNames(){ return deviceNames(ma_device_type_playback); }
std::vector<std::string> inputDeviceNames(){ return deviceNames(ma_device_type_capture); }

static bool startEngine(const std::string& outputDevice, std::string& error){
    ma_device_id id;
    ma_engine_config config = ma_engine_config_init();
    config.pContext = &audio.context;
    config.pPlaybackDeviceID = findDevice(ma_device_type_playback, outputDevice, id) ? &id : nullptr; // not found: system default
    ma_result result = ma_engine_init(&config, &audio.engine);
    if (result != MA_SUCCESS){
        error = std::string("could not start audio output: ") + ma_result_description(result);
        return false;
    }
    audio.engineReady = true;
    return true;
}

bool initAudio(const std::string& outputDevice, std::string& error){
    ma_result result = ma_context_init(nullptr, 0, nullptr, &audio.context); // every backend available, best first
    if (result != MA_SUCCESS){
        error = std::string("could not connect to the system's audio: ") + ma_result_description(result);
        return false;
    }
    audio.contextReady = true;
    return startEngine(outputDevice, error);
}

static void releaseVoice(Voice& voice){
    if (!voice.ready) return;
    ma_sound_uninit(&voice.sound); // detaches from the engine first, so the audio thread stops reading the buffer
    ma_audio_buffer_uninit(&voice.buffer);
    voice.ready = false;
}

static void stopEngine(){
    for (Voice& voice : audio.voices) releaseVoice(voice);
    unloadSong();
    if (audio.engineReady) ma_engine_uninit(&audio.engine);
    audio.engineReady = false;
}

void closeAudio(){
    stopCapture();
    stopEngine();
    if (audio.contextReady) ma_context_uninit(&audio.context);
    audio.contextReady = false;
}

bool setOutputDevice(const std::string& outputDevice, std::string& error){
    float volume = audio.engineReady ? ma_engine_get_volume(&audio.engine) : 1.0f;
    stopEngine();
    if (!startEngine(outputDevice, error)) return false;
    ma_engine_set_volume(&audio.engine, volume);
    // A custom preview sound was decoded at the old device's sample rate: decode it again
    std::string ignored;
    if (!setPreviewSound(audio.previewSoundName, audio.soundsDir, ignored)) setPreviewSound("pluck", audio.soundsDir, ignored);
    return true;
}

const char* outputDeviceName(){
    return audio.engineReady ? ma_engine_get_device(&audio.engine)->playback.name : "none";
}

void setMasterVolume(float volume){
    if (audio.engineReady) ma_engine_set_volume(&audio.engine, volume);
}

const char* audioBackendName(){
    return audio.contextReady ? ma_get_backend_name(audio.context.backend) : "none";
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

bool startCapture(const std::string& inputDevice, std::string& error){
    stopCapture();
    if (!audio.contextReady){
        error = "audio is not running";
        return false;
    }

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
    ma_device_id id;
    if (findDevice(ma_device_type_capture, inputDevice, id)) config.capture.pDeviceID = &id; // not found: system default
    result = ma_device_init(&audio.context, &config, &audio.captureDevice);
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

static bool isSoundFile(const std::filesystem::path& path){
    std::string extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c){ return (char)std::tolower(c); });
    for (const char* known : SOUND_FILE_EXTENSIONS) if (extension == known) return true;
    return false;
}

std::vector<std::string> previewSoundNames(const std::string& soundsDir){
    std::vector<std::string> names(BUILT_IN_PREVIEW_SOUNDS, BUILT_IN_PREVIEW_SOUNDS + BUILT_IN_PREVIEW_SOUND_COUNT);
    std::vector<std::string> files;
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(soundsDir, ec)){
        if (entry.is_regular_file() && isSoundFile(entry.path())) files.push_back(entry.path().filename().string());
    }
    std::sort(files.begin(), files.end());
    names.insert(names.end(), files.begin(), files.end());
    return names;
}

bool setPreviewSound(const std::string& name, const std::string& soundsDir, std::string& error){
    // Voices may be reading the current custom sound: stop them before it changes
    for (Voice& voice : audio.voices) releaseVoice(voice);
    audio.soundsDir = soundsDir;

    for (const char* builtIn : BUILT_IN_PREVIEW_SOUNDS){
        if (name == builtIn){
            audio.previewSoundName = name;
            audio.customSound.clear();
            return true;
        }
    }
    if (!audio.engineReady){
        error = "audio is not running";
        return false;
    }

    // A sound file: decode it once, to mono floats at the engine's rate, so playing it costs nothing extra
    std::string path = (std::filesystem::path(soundsDir) / name).string();
    ma_uint32 sampleRate = ma_engine_get_sample_rate(&audio.engine);
    ma_decoder_config config = ma_decoder_config_init(ma_format_f32, 1, sampleRate);
    ma_uint64 frameCount = 0;
    void* frames = nullptr;
    ma_result result = ma_decode_file(path.c_str(), &config, &frameCount, &frames);
    if (result != MA_SUCCESS){
        error = path + ": could not load sound: " + ma_result_description(result);
        return false;
    }
    // Skip silence at the start (MP3 encoders add some, recordings often have some): a sound that starts
    // late would make every preview and click sound late
    const float* samples = (const float*)frames;
    ma_uint64 first = 0;
    while (first < frameCount && std::fabs(samples[first]) < SILENCE_LEVEL) first++;
    ma_uint64 keep = std::min<ma_uint64>(frameCount - first, (ma_uint64)(MAX_CUSTOM_SOUND_S * sampleRate));
    audio.customSound.assign(samples + first, samples + first + keep);
    ma_free(frames, nullptr); // miniaudio allocated it; it's copied into our vector now
    if (audio.customSound.empty()){
        error = path + ": the sound file is empty";
        return false;
    }
    if (first + keep < frameCount){ // cut short: fade the new end so it doesn't click
        int fade = std::min((int)keep, (int)(0.01f * sampleRate));
        for (int i = 0; i < fade; i++) audio.customSound[keep - 1 - i] *= (float)i / fade;
    }

    // Find the sound's own pitch, so notes can be played by speeding it up or slowing it down.
    // Measured just after the attack, where most sounds are loudest and steadiest.
    PitchDetector detector;
    initPitchDetector(detector, (int)sampleRate, 30.0f, 2000.0f);
    int window = pitchWindowSize(detector);
    int start = std::min((int)(0.05f * sampleRate), std::max(0, (int)audio.customSound.size() - window));
    audio.customSoundRoot = 0.0f;
    if ((int)audio.customSound.size() >= window){
        PitchResult pitch = detectPitch(detector, audio.customSound.data() + start, window);
        if (pitch.frequency > 0.0f && pitch.clarity > 0.8f) audio.customSoundRoot = pitch.frequency;
    }
    audio.previewSoundName = name;
    return true;
}

const char* previewSoundName(){
    return audio.previewSoundName.c_str();
}

bool previewSoundHasPitch(){
    return audio.customSound.empty() || audio.customSoundRoot > 0.0f;
}

void setPreviewVolume(float volume){
    audio.previewVolume = volume;
}

void playPreview(float frequency){
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

    // Built-in sounds are rendered at the engine's own sample rate and pitch, so nothing is resampled.
    // A custom sound plays from the shared decoded copy, re-pitched to the note if it has a pitch.
    ma_uint32 sampleRate = ma_engine_get_sample_rate(&audio.engine);
    const float* data;
    size_t frames;
    if (audio.customSound.empty()){
        voice->samples.resize((size_t)(PREVIEW_LENGTH_S * sampleRate)); // keeps its memory between notes: no allocation after the first
        renderBuiltInSound(audio.previewSoundName.c_str(), voice->samples.data(), (int)voice->samples.size(), frequency,
                           (int)sampleRate, (unsigned)audio.previewCount);
        data = voice->samples.data();
        frames = voice->samples.size();
    } else {
        data = audio.customSound.data();
        frames = audio.customSound.size();
    }

    ma_audio_buffer_config config = ma_audio_buffer_config_init(ma_format_f32, 1, frames, data, nullptr);
    config.sampleRate = sampleRate;
    if (ma_audio_buffer_init(&config, &voice->buffer) != MA_SUCCESS) return;
    if (ma_sound_init_from_data_source(&audio.engine, &voice->buffer, MA_SOUND_FLAG_NO_SPATIALIZATION, nullptr, &voice->sound) != MA_SUCCESS){
        ma_audio_buffer_uninit(&voice->buffer);
        return;
    }
    voice->ready = true;
    voice->startedAt = ++audio.previewCount;
    if (!audio.customSound.empty() && audio.customSoundRoot > 0.0f){
        ma_sound_set_pitch(&voice->sound, frequency / audio.customSoundRoot); // 2.0 = an octave up (and twice as short)
    }
    ma_sound_set_volume(&voice->sound, audio.previewVolume);
    ma_sound_start(&voice->sound);
}
