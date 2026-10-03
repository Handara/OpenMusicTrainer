#include "audio/audio.h"

#include "audio/asiodriver.h"
#include "core/inputs.h"
#include "core/pitch.h"
#include "core/settings.h"
#include "core/synth.h"
#include "core/timestretch.h"
#include "core/tonechain.h"
#include "miniaudio.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <cmath>
#include <cstring>
#include <thread>
#include <vector>

// songPosition() runs a smooth clock on the wall clock and pulls it toward the audio position each call.
// Each call closes this fraction of the gap: small enough to average out the audio position's ~10 ms steps.
const double DRIFT_CORRECTION = 0.05;
// A bigger gap means the audio really jumped. Ahead: snap to it. Behind (the device starting up, a stall):
// wait for it instead, because a game clock must never run backwards.
const double SNAP_THRESHOLD_S = 0.1;
// playSongFrom starts the song this far ahead on the engine's clock: time to schedule the first clicks and notes
// with it, so none of them is late
const double SONG_START_LEAD_S = 0.1;

// Captured audio waiting for the main thread. At 48 kHz this is ~0.34 s: room for several slow frames.
const ma_uint32 CAPTURE_BUFFER_FRAMES = 16384;
// Hearing the instrument (setMonitor): at most this many of an interface's inputs are mixed, this many frames wait for
// the output at most (~170 ms at 48 kHz: only when something stalls), and the capture thread mixes this many at a time
const int MAX_MONITOR_INPUTS = 32;
const ma_uint32 MONITOR_BUFFER_FRAMES = 8192;
const ma_uint32 MONITOR_CHUNK = 1024;
// The duplex device's period asked for (startDuplexCapture): ~3 ms at 48 kHz; Windows grants what the device can
const ma_uint32 LOW_LATENCY_PERIOD_FRAMES = 128;
const double DUPLEX_START_S = 0.3; // a duplex device that hasn't called by then isn't going to
// How an ASIO driver is named among the input devices: "ASIO: Focusrite USB ASIO"
const char* const ASIO_PREFIX = "ASIO: ";

// Preview sounds (e.g. the editor playing a note you place). Several can ring at once, like real strings.
const int VOICE_COUNT = 8;
const float PREVIEW_LENGTH_S = 1.5f;     // built-in sounds
const float CLICK_LENGTH_S = 0.08f;
const float CLICK_VOLUME = 0.8f;
const float MAX_CUSTOM_SOUND_S = 3.0f;   // longer sound files are cut (and faded) here
const char* const SOUND_FILE_EXTENSIONS[] = { ".wav", ".mp3", ".flac", ".ogg" }; // what miniaudio decodes (.ogg: stb_vorbis)
const float SILENCE_LEVEL = 0.001f;      // -60 dB: quieter than this at the start of a sound file counts as silence

// One preview sound: its samples, and the miniaudio objects playing them.
// ma_audio_buffer reads straight from the samples (no copy), so they must stay alive while it plays:
// built-in sounds are rendered into the voice's own `samples`, custom sounds are read from `customSound`.
struct Voice {
    std::vector<float> samples;
    ma_audio_buffer buffer;
    ma_sound sound;
    bool ready = false;
    ma_uint64 startFrame = 0; // engine time (in frames) when it starts sounding: when all voices are busy,
                              // the one that started earliest is reused (it has faded the most)
};

// A song read from a function (loadSongFromReader), as a miniaudio "data source": a struct that starts with
// ma_data_source_base, plus the functions miniaudio calls on it (the vtable below). onRead runs on the audio thread.
struct ReaderSource {
    ma_data_source_base base; // must come first: miniaudio treats a pointer to this struct as a data source
    SongReader reader = nullptr;
    void* user = nullptr;
    ma_uint32 sampleRate = 0;
    ma_uint32 channels = 0;
    ma_uint64 length = 0;
    std::atomic<ma_uint64> cursor{0}; // frames read so far: written by the audio thread, read by songPosition()
};

// The song at another speed than its own (setSongSpeed), as a data source: its file decoded and stretched in time
// (core/timestretch) as the audio thread reads it. Its cursor is in the song's own frames, so the game's clock stays
// the song's: at half speed it simply moves half as fast.
struct StretchSource {
    ma_data_source_base base; // must come first, as for ReaderSource
    ma_decoder decoder;
    TimeStretch stretch;
    ma_uint32 sampleRate = 0;
    ma_uint32 channels = 0;
    ma_uint64 length = 0;               // the song's frames
    double position = 0.0;              // the song frame reached, played out frame by frame at the speed of the moment
    std::atomic<float> speed{1.0f};     // the speed wanted: set by the main thread (setSongSpeedLive), taken up by the audio thread
    std::atomic<ma_uint64> cursor{0};   // the song frame being heard: written by the audio thread, read by songPosition()
    std::vector<float> scratch;         // decoded frames on their way in: sized once, so the audio thread doesn't allocate
    bool decoderEnded = false;
};

// The instrument being heard (setMonitor), as a data source the engine plays: it reads what the capture thread mixed
// into the monitor buffer and puts it through the tone. It never ends: with nothing played it gives silence.
struct MonitorSource {
    ma_data_source_base base; // must come first, as for ReaderSource
    ma_uint32 sampleRate = 48000;
};

// The tone the real sound goes through, as each audio thread that plays it has it: its own effects' memory, and the
// version of the tone it took last (setMonitorTone)
struct ToneRunner {
    ToneChain chain;
    unsigned seen = ~0u;
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
    ma_uint32 captureChannels = 1; // the input device's inputs, kept apart in the capture buffer
    ReaderSource readerSource; // the song's source when it comes from a function
    bool songFromReader = false;
    StretchSource stretchSource; // or when it's played at another speed than its own
    bool songStretched = false;
    std::string songPath;        // the song's file, to open it again at another speed
    float songSpeed = 1.0f;      // see setSongSpeed
    bool alwaysStretch = false;  // see keepSongStretched
    bool looping = false;
    double songStartTime = 0.0;     // playSongFrom: the engine time (audioTime) the song starts playing at...
    double songStartPosition = 0.0; // ...and the song position it starts from

    // Clock smoothing: the audio position only changes when the audio thread processes a chunk,
    // so on its own it moves in steps. smoothTime advances continuously and follows it.
    // When looping, smoothTime keeps counting past the loop point; songPosition() wraps it on return.
    double smoothTime = 0.0;
    double lastWallTime = 0.0;

    // The engine's own clock, smoothed the same way, for things that run without a song (metronome, drills)
    double engineSmoothTime = 0.0;
    double engineLastWallTime = 0.0;
    bool engineClockStarted = false;

    // Input (microphone / instrument). The audio thread writes into captureBuffer, the main thread reads from it.
    ma_device captureDevice;
    ma_pcm_rb captureBuffer;
    bool captureReady = false;
    bool exclusiveWanted = true;  // see setExclusiveCapture
    bool captureExclusive = false;
    std::string asioDevice;        // the capture runs on this ASIO driver ("ASIO: ..."), "" when on Windows' own
    std::string captureOpenedFor;  // the input device asked for when the capture was opened
    bool captureReader = false;    // a screen is reading the capture (startCapture); the monitor may keep it open without
    bool captureStale = false;     // opened the way it no longer should be (exclusive mode was switched): reopen

    // Hearing the instrument (setMonitor). The capture thread mixes the instrument's inputs into monitorBuffer (mono);
    // the engine plays it through monitorSource and monitorSound.
    bool monitorWanted = false;
    std::string monitorDevice;
    std::vector<int> monitorInputs;  // empty: every input but monitorExcluded
    int monitorExcluded = -1;
    std::atomic<float> monitorWeights[MAX_MONITOR_INPUTS] = {}; // per input, read by the capture thread
    std::atomic<bool> monitorGate{false};
    std::atomic<ma_uint32> captureBurst{0}; // the most frames the input has handed over at once lately
    ma_pcm_rb monitorBuffer;
    bool monitorBufferReady = false;
    float monitorScratch[MONITOR_CHUNK] = {}; // the capture thread's: one chunk mixed down, before it goes in
    MonitorSource monitorSource;
    ma_sound monitorSound;
    bool monitorSoundReady = false;
    // The tone (setMonitorTone): written by the main thread, taken by the audio threads between buffers. Its version is
    // odd while it's being written, and goes up each time.
    ToneParameters toneShared;
    std::atomic<unsigned> toneVersion{0};
    ToneRunner engineTone;              // the engine's thread's (the monitor source)
    std::atomic<bool> asioGate{false}; // ASIO calls as soon as it starts: its samples go in once the buffer exists
    // The mixer driven by a callback of lahn's own instead of a device of its own: the ASIO driver's (startAsioCapture)
    // or a duplex device's (startDuplexCapture), both ways in one call, the instrument heard straight through. The
    // input then stays open between screens, since everything heard depends on it.
    std::atomic<bool> engineExternal{false};
    bool closing = false;               // closeAudio: nothing is started again on the way out
    std::string outputDeviceWanted;     // the output device the settings ask for, used again when that callback stops
    ma_device duplexDevice;             // input and output as one device, in Windows' low-latency mode where it can
    bool duplexReady = false;
    std::atomic<long> duplexCalls{0};   // its callbacks so far: proof it's really running
    const float* directInput = nullptr; // the callback's latest input, for the same callback's direct monitor
    int directInputFrames = 0;
    std::atomic<ma_uint32> directRate{48000}; // the rate that callback runs at
    ToneRunner directTone;              // the direct monitor's (that callback's only)

    Voice voices[VOICE_COUNT];
    Voice synthVoices[2];  // the synth bass (playSynthNote): one playing, the last fading out while it starts
    int synthCurrent = 0;
    std::atomic<bool> monitorSynth{true}; // the monitor plays notes on the synth (read by the main thread), not the input
    Voice wake; // plays a moment of silence when an engine starts (see wakeEngineClock): not part of the pool,
                // so nothing that stops the preview voices can cut it
    unsigned long long previewCount = 0;
    float previewVolume = 0.6f;
    float songVolume = 1.0f;
    float hitSoundVolume = 0.5f; // see setHitSoundVolume
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
std::vector<std::string> inputDeviceNames(){
    std::vector<std::string> names = deviceNames(ma_device_type_capture);
    for (const std::string& driver : asioDriverNames()) names.push_back(ASIO_PREFIX + driver);
    return names;
}

static void releaseVoice(Voice& voice);
static void startVoice(Voice& voice, const float* data, size_t frames, float pitchRatio, float volume, unsigned long long startFrame);
static void startPreview(float frequency, ma_uint64 startFrame, const char* builtIn = nullptr);
static void startMonitorSound();
static void stopMonitorSound();
static bool openCapture(const std::string& inputDevice, std::string& error);
static void closeCapture();

// On some systems (PulseAudio under WSL, at least) the engine's clock doesn't start until the first sound plays,
// and anything timed on it (the metronome, drills) would wait forever. A moment of silence gets it running for good.
static void wakeEngineClock(){
    audio.wake.samples.assign((size_t)(0.05f * ma_engine_get_sample_rate(&audio.engine)), 0.0f);
    startVoice(audio.wake, audio.wake.samples.data(), audio.wake.samples.size(), 1.0f, 1.0f, 0);
}

static bool startEngine(const std::string& outputDevice, std::string& error){
    audio.outputDeviceWanted = outputDevice;
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
    audio.engineClockStarted = false; // a new engine counts from zero again
    wakeEngineClock();
    if (audio.monitorWanted) startMonitorSound(); // a new output: the instrument is heard on it too
    return true;
}

// The engine with no device: lahn's own callback reads it (renderOutput), at that callback's rate, in stereo
static bool startEngineExternal(ma_uint32 sampleRate, std::string& error){
    ma_engine_config config = ma_engine_config_init();
    config.pContext = &audio.context;
    config.noDevice = MA_TRUE;
    config.channels = 2;
    config.sampleRate = sampleRate;
    ma_result result = ma_engine_init(&config, &audio.engine);
    if (result != MA_SUCCESS){
        error = std::string("could not start audio output: ") + ma_result_description(result);
        return false;
    }
    audio.engineReady = true;
    audio.engineClockStarted = false;
    wakeEngineClock();
    if (audio.monitorWanted) startMonitorSound();
    audio.directRate = sampleRate;
    audio.engineExternal = true; // the callback may read it from now on
    return true;
}

bool initAudio(const std::string& outputDevice, std::string& error){
    ma_result result = ma_context_init(nullptr, 0, nullptr, &audio.context); // every backend available, best first
    if (result != MA_SUCCESS){
        error = std::string("could not connect to the system's audio: ") + ma_result_description(result);
        return false;
    }
    audio.contextReady = true;
    initToneChain(audio.engineTone.chain, 48000); // their memory, once: the audio threads never allocate
    initToneChain(audio.directTone.chain, 48000);
    return startEngine(outputDevice, error);
}

static void releaseVoice(Voice& voice){
    if (!voice.ready) return;
    ma_sound_uninit(&voice.sound); // detaches from the engine first, so the audio thread stops reading the buffer
    ma_audio_buffer_uninit(&voice.buffer);
    voice.ready = false;
}

static void stopEngine(){
    stopMonitorSound(); // before the engine it plays on
    for (Voice& voice : audio.synthVoices) releaseVoice(voice);
    for (Voice& voice : audio.voices) releaseVoice(voice);
    releaseVoice(audio.wake);
    unloadSong();
    if (audio.engineReady) ma_engine_uninit(&audio.engine);
    audio.engineReady = false;
}

void closeAudio(){
    audio.closing = true;
    audio.monitorWanted = false;
    audio.monitorGate = false;
    closeCapture();
    if (audio.monitorBufferReady) ma_pcm_rb_uninit(&audio.monitorBuffer);
    audio.monitorBufferReady = false;
    stopEngine();
    if (audio.contextReady) ma_context_uninit(&audio.context);
    audio.contextReady = false;
}

bool setOutputDevice(const std::string& outputDevice, std::string& error){
    if (audio.engineExternal && !audio.duplexReady){
        audio.outputDeviceWanted = outputDevice; // lahn plays through the ASIO driver: this one's for when it closes
        return true;
    }
    if (audio.duplexReady){
        // The duplex device plays through the output device: it's opened again with the new one
        audio.outputDeviceWanted = outputDevice;
        std::string input = audio.captureOpenedFor;
        bool reader = audio.captureReader;
        closeCapture();
        bool opened = openCapture(input, error);
        audio.captureReader = reader && opened;
        return opened || audio.engineReady;
    }
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
    if (audio.duplexReady) return audio.duplexDevice.playback.name;
    if (audio.engineExternal) return audio.asioDevice.c_str();
    return audio.engineReady ? ma_engine_get_device(&audio.engine)->playback.name : "none";
}

bool outputIsAsio(){
    return audio.engineExternal && !audio.duplexReady;
}

double outputLatencySeconds(){
    if (!audio.engineReady) return 0.0;
    if (audio.duplexReady){
        const ma_device& device = audio.duplexDevice;
        return (double)device.playback.internalPeriodSizeInFrames * device.playback.internalPeriods / std::max<ma_uint32>(1, device.playback.internalSampleRate);
    }
    if (audio.engineExternal) return asioOutputLatencyFrames() / std::max(1.0, asioSampleRate());
    const ma_device* device = ma_engine_get_device(&audio.engine);
    return (double)device->playback.internalPeriodSizeInFrames * device->playback.internalPeriods / std::max<ma_uint32>(1, device->playback.internalSampleRate);
}

void setMasterVolume(float volume){
    if (audio.engineReady) ma_engine_set_volume(&audio.engine, volume);
}

const char* audioBackendName(){
    return audio.contextReady ? ma_get_backend_name(audio.context.backend) : "none";
}

static const ma_uint32 STRETCH_BLOCK = 4096; // frames decoded at a time for the stretch

static ma_result stretchRead(ma_data_source* source, void* out, ma_uint64 frameCount, ma_uint64* framesRead){
    StretchSource* self = (StretchSource*)source;
    TimeStretch& stretch = self->stretch;
    stretch.speed = self->speed.load(std::memory_order_relaxed); // a new speed takes over from the next piece
    ma_uint64 done = 0;
    float* to = (float*)out;
    while (done < frameCount){
        int got = takeTimeStretch(stretch, to + done * self->channels, (int)(frameCount - done));
        done += (ma_uint64)got;
        if (done == frameCount) break;
        int wants = timeStretchWants(stretch);
        if (wants == 0 && got == 0) break; // played out
        if (wants > 0){
            ma_uint64 decoded = 0;
            ma_uint64 asked = std::min<ma_uint64>((ma_uint64)wants, STRETCH_BLOCK);
            if (!self->decoderEnded) ma_decoder_read_pcm_frames(&self->decoder, self->scratch.data(), asked, &decoded);
            if (decoded > 0) feedTimeStretch(stretch, self->scratch.data(), (int)decoded);
            if (decoded < asked){
                self->decoderEnded = true;
                endTimeStretch(stretch);
            }
        }
    }
    self->position += done * (double)stretch.speed;
    const double heard = self->position + timeStretchLag(stretch);
    self->cursor = (ma_uint64)std::max(0.0, std::min(heard, (double)self->length));
    *framesRead = done;
    return done == 0 ? MA_AT_END : MA_SUCCESS;
}

static ma_result stretchSeek(ma_data_source* source, ma_uint64 frame){
    StretchSource* self = (StretchSource*)source;
    ma_result result = ma_decoder_seek_to_pcm_frame(&self->decoder, frame);
    resetTimeStretch(self->stretch, (long long)frame);
    self->position = (double)frame;
    self->cursor = frame;
    self->decoderEnded = false;
    return result;
}

static ma_result stretchFormat(ma_data_source* source, ma_format* format, ma_uint32* channels, ma_uint32* sampleRate,
                               ma_channel* channelMap, size_t channelMapCapacity){
    StretchSource* self = (StretchSource*)source;
    *format = ma_format_f32;
    *channels = self->channels;
    *sampleRate = self->sampleRate;
    ma_channel_map_init_standard(ma_standard_channel_map_default, channelMap, channelMapCapacity, self->channels);
    return MA_SUCCESS;
}

static ma_result stretchCursor(ma_data_source* source, ma_uint64* cursor){
    *cursor = ((StretchSource*)source)->cursor;
    return MA_SUCCESS;
}

static ma_result stretchLength(ma_data_source* source, ma_uint64* length){
    *length = ((StretchSource*)source)->length;
    return MA_SUCCESS;
}

static ma_data_source_vtable STRETCH_VTABLE = { stretchRead, stretchSeek, stretchFormat, stretchCursor, stretchLength, nullptr, 0 };

// The song's file opened as the sound the engine plays: streamed as it is, or through the stretch at another speed
static bool openSongSound(const std::string& path, std::string& error){
    if (audio.songSpeed == 1.0f && !audio.alwaysStretch){
        // STREAM decodes a little at a time instead of the whole file up front
        // (a 5 minute song fully decoded is ~100 MB). NO_SPATIALIZATION skips unneeded 3D audio processing.
        ma_uint32 flags = MA_SOUND_FLAG_STREAM | MA_SOUND_FLAG_NO_SPATIALIZATION;
        ma_result result = ma_sound_init_from_file(&audio.engine, path.c_str(), flags, nullptr, nullptr, &audio.song);
        if (result != MA_SUCCESS){
            error = path + ": could not load audio: " + ma_result_description(result);
            return false;
        }
        return true;
    }
    StretchSource& source = audio.stretchSource;
    ma_decoder_config decoderConfig = ma_decoder_config_init(ma_format_f32, 0, 0); // its own channels and rate
    ma_result result = ma_decoder_init_file(path.c_str(), &decoderConfig, &source.decoder);
    if (result != MA_SUCCESS){
        error = path + ": could not load audio: " + ma_result_description(result);
        return false;
    }
    source.channels = source.decoder.outputChannels;
    source.sampleRate = source.decoder.outputSampleRate;
    ma_uint64 length = 0;
    ma_decoder_get_length_in_pcm_frames(&source.decoder, &length);
    source.length = length;
    source.scratch.assign((size_t)STRETCH_BLOCK * source.channels, 0.0f);
    initTimeStretch(source.stretch, (int)source.channels, (int)source.sampleRate, audio.songSpeed);
    source.position = 0.0;
    source.speed = audio.songSpeed;
    source.cursor = 0;
    source.decoderEnded = false;
    ma_data_source_config config = ma_data_source_config_init();
    config.vtable = &STRETCH_VTABLE;
    result = ma_data_source_init(&config, &source.base);
    if (result == MA_SUCCESS) result = ma_sound_init_from_data_source(&audio.engine, &source.base, MA_SOUND_FLAG_NO_SPATIALIZATION, nullptr, &audio.song);
    if (result != MA_SUCCESS){
        ma_decoder_uninit(&source.decoder);
        error = path + ": could not play it at another speed: " + ma_result_description(result);
        return false;
    }
    audio.songStretched = true;
    return true;
}

static void closeSongSound(){
    // Uninitializing the sound detaches it from the audio thread, so its source is never read after this
    if (audio.songReady) ma_sound_uninit(&audio.song);
    if (audio.songStretched){
        ma_data_source_uninit(&audio.stretchSource.base);
        ma_decoder_uninit(&audio.stretchSource.decoder);
    }
    audio.songStretched = false;
}

bool loadSong(const std::string& path, std::string& error){
    unloadSong();
    if (!openSongSound(path, error)) return false;
    audio.songPath = path;
    ma_sound_get_data_format(&audio.song, nullptr, nullptr, &audio.songSampleRate, nullptr, 0);
    ma_uint64 lengthFrames = 0;
    ma_sound_get_length_in_pcm_frames(&audio.song, &lengthFrames);
    audio.songLengthS = (double)lengthFrames / audio.songSampleRate;
    // A streamed Ogg file doesn't know its length; a decoder of its own finds it (stb_vorbis reads the last page)
    if (lengthFrames == 0){
        ma_decoder decoder;
        if (ma_decoder_init_file(path.c_str(), nullptr, &decoder) == MA_SUCCESS){
            if (ma_decoder_get_length_in_pcm_frames(&decoder, &lengthFrames) == MA_SUCCESS && decoder.outputSampleRate > 0){
                audio.songLengthS = (double)lengthFrames / decoder.outputSampleRate;
            }
            ma_decoder_uninit(&decoder);
        }
    }
    audio.songReady = true;
    ma_sound_set_volume(&audio.song, audio.songVolume);
    return true;
}

void setSongSpeed(float speed){
    speed = std::clamp(speed, 0.02f, 2.0f);
    if (speed == audio.songSpeed && (audio.songStretched || !audio.alwaysStretch)) return;
    audio.songSpeed = speed;
    // The song open now is opened again, to be played the new way (stopped: it's started again from where it's wanted)
    if (!audio.songReady || audio.songFromReader || audio.songPath.empty()) return;
    closeSongSound();
    audio.songReady = false;
    std::string error;
    if (!openSongSound(audio.songPath, error)) return;
    audio.songReady = true;
    ma_sound_set_volume(&audio.song, audio.songVolume);
}

float songSpeed(){
    return audio.songSpeed;
}

void setSongSpeedLive(float speed){
    speed = std::clamp(speed, 0.02f, 2.0f);
    if (!audio.songStretched){
        setSongSpeed(speed);
        return;
    }
    audio.songSpeed = speed; // the clock follows at once; the sound, from its next piece
    audio.stretchSource.speed.store(speed, std::memory_order_relaxed);
}

void keepSongStretched(bool on){
    audio.alwaysStretch = on;
}

static ma_result readerRead(ma_data_source* source, void* out, ma_uint64 frameCount, ma_uint64* framesRead){
    ReaderSource* self = (ReaderSource*)source;
    int read = self->reader(self->user, (float*)out, (int)frameCount);
    self->cursor += read;
    *framesRead = read;
    return read == 0 ? MA_AT_END : MA_SUCCESS;
}

// It plays from the start only: the one seek allowed is to where it already is (playSong seeks to 0 before starting)
static ma_result readerSeek(ma_data_source* source, ma_uint64 frame){
    return frame == ((ReaderSource*)source)->cursor ? MA_SUCCESS : MA_NOT_IMPLEMENTED;
}

static ma_result readerFormat(ma_data_source* source, ma_format* format, ma_uint32* channels, ma_uint32* sampleRate,
                              ma_channel* channelMap, size_t channelMapCapacity){
    ReaderSource* self = (ReaderSource*)source;
    *format = ma_format_f32;
    *channels = self->channels;
    *sampleRate = self->sampleRate;
    ma_channel_map_init_standard(ma_standard_channel_map_default, channelMap, channelMapCapacity, self->channels);
    return MA_SUCCESS;
}

static ma_result readerCursor(ma_data_source* source, ma_uint64* cursor){
    *cursor = ((ReaderSource*)source)->cursor;
    return MA_SUCCESS;
}

static ma_result readerLength(ma_data_source* source, ma_uint64* length){
    *length = ((ReaderSource*)source)->length;
    return MA_SUCCESS;
}

static ma_data_source_vtable READER_VTABLE = { readerRead, readerSeek, readerFormat, readerCursor, readerLength, nullptr, 0 };

bool loadSongFromReader(SongReader reader, void* user, int sampleRate, int channels, double lengthSeconds, std::string& error){
    unloadSong();
    ReaderSource& source = audio.readerSource;
    ma_data_source_config config = ma_data_source_config_init();
    config.vtable = &READER_VTABLE;
    ma_result result = ma_data_source_init(&config, &source.base);
    if (result == MA_SUCCESS){
        source.reader = reader;
        source.user = user;
        source.sampleRate = (ma_uint32)sampleRate;
        source.channels = (ma_uint32)channels;
        source.length = (ma_uint64)(lengthSeconds * sampleRate);
        source.cursor = 0;
        result = ma_sound_init_from_data_source(&audio.engine, &source.base, MA_SOUND_FLAG_NO_SPATIALIZATION, nullptr, &audio.song);
    }
    if (result != MA_SUCCESS){
        error = std::string("could not start the video's sound: ") + ma_result_description(result);
        return false;
    }
    audio.songSampleRate = (ma_uint32)sampleRate;
    audio.songLengthS = lengthSeconds;
    audio.songReady = true;
    ma_sound_set_volume(&audio.song, audio.songVolume);
    audio.songFromReader = true;
    return true;
}

void unloadSong(){
    // Uninitializing the sound detaches it from the audio thread, so the reader is never called after this
    closeSongSound();
    if (audio.songFromReader) ma_data_source_uninit(&audio.readerSource.base);
    audio.songPath.clear();
    audio.songReady = false;
    audio.songFromReader = false;
    audio.songStartTime = 0.0;
}

void playSong(bool loop){
    if (!audio.songReady) return;
    audio.looping = loop;
    ma_sound_set_looping(&audio.song, loop ? MA_TRUE : MA_FALSE);
    ma_sound_seek_to_pcm_frame(&audio.song, 0);
    ma_sound_set_start_time_in_pcm_frames(&audio.song, 0); // at once, even after a playSongFrom
    audio.songStartTime = 0.0;
    audio.smoothTime = 0.0;
    audio.lastWallTime = wallClockSeconds();
    ma_sound_start(&audio.song);
}

double playSongFrom(double seconds, double notBefore){
    if (!audio.songReady || !audio.engineReady || audio.songFromReader) return -1.0;
    audio.looping = false;
    ma_sound_set_looping(&audio.song, MA_FALSE);
    ma_sound_stop(&audio.song);
    // Before where the audio may start (its first sample, or a trimmed song's start), the song waits that much
    // longer and plays from there
    const double from = std::max(0.0, notBefore), early = std::max(0.0, from - seconds);
    ma_sound_seek_to_pcm_frame(&audio.song, (ma_uint64)std::llround(std::max(from, seconds) * audio.songSampleRate));
    ma_uint32 engineRate = ma_engine_get_sample_rate(&audio.engine);
    double wait = SONG_START_LEAD_S + early / audio.songSpeed; // the song's seconds, slower or faster as it plays
    ma_uint64 start = ma_engine_get_time_in_pcm_frames(&audio.engine) + (ma_uint64)std::llround(wait * engineRate);
    ma_sound_set_start_time_in_pcm_frames(&audio.song, start);
    audio.songStartTime = (double)start / engineRate;
    audio.songStartPosition = std::max(from, seconds);
    audio.smoothTime = seconds;
    audio.lastWallTime = wallClockSeconds();
    ma_sound_start(&audio.song);
    return (double)(start - (ma_uint64)std::llround(early / audio.songSpeed * engineRate)) / engineRate;
}

void stopSong(){
    if (audio.songReady) ma_sound_stop(&audio.song);
}

void setSongVolume(float volume){
    audio.songVolume = std::clamp(volume, 0.0f, 1.0f);
    if (audio.songReady) ma_sound_set_volume(&audio.song, audio.songVolume);
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

    // Started for a moment from now (playSongFrom): until then the audio stands still (and miniaudio calls it not
    // playing), so the position counts up to the start on the engine's clock instead
    if (audio.songStartTime > 0.0){
        double untilStart = audio.songStartTime - ::audioTime();
        if (untilStart > 0.0){
            audio.smoothTime = audio.songStartPosition - untilStart * audio.songSpeed; // the song's seconds
            audio.lastWallTime = now;
            return audio.smoothTime;
        }
    }
    if (!ma_sound_is_playing(&audio.song)){
        audio.smoothTime = audioTime;
        audio.lastWallTime = now;
        return audioTime;
    }

    double advance = (now - audio.lastWallTime) * audio.songSpeed; // in the song's seconds
    double previous = audio.smoothTime;
    audio.smoothTime += advance;
    audio.lastWallTime = now;

    // Where the smooth clock is inside the song, and how far the audio is from it
    double wrapped = audio.looping ? std::fmod(audio.smoothTime, length) : audio.smoothTime;
    double drift = audioTime - wrapped;
    if (audio.looping){
        // Around the loop point one side has wrapped and the other hasn't yet: 8.99 vs 0.01 is 0.02 apart, not 8.98
        if (drift > length / 2) drift -= length;
        if (drift < -length / 2) drift += length;
    }

    if (drift > SNAP_THRESHOLD_S) audio.smoothTime += drift;          // the audio is well ahead: catch up at once
    else if (drift < -SNAP_THRESHOLD_S) audio.smoothTime -= advance;  // well behind: hold still until it catches up
    else audio.smoothTime += drift * DRIFT_CORRECTION;
    audio.smoothTime = std::max(audio.smoothTime, previous); // a small correction must not step it back either

    return audio.looping ? std::fmod(audio.smoothTime, length) : audio.smoothTime;
}

// Fresh input samples into the capture buffer, for the main thread. Runs on the audio thread (miniaudio's or the
// ASIO driver's), every millisecond or few. Rules for this function: no allocation, no locks, no file or console
// I/O. Anything slow here makes the device miss its deadline and drop samples. So: copy into the ring and return.
static void writeCapture(const float* samples, ma_uint32 frameCount){
    const ma_uint32 channels = audio.captureChannels;
    ma_uint32 written = 0;
    while (written < frameCount){
        // The free space may wrap around the end of the ring, so it can come in two pieces: loop
        ma_uint32 chunk = frameCount - written;
        void* destination;
        if (ma_pcm_rb_acquire_write(&audio.captureBuffer, &chunk, &destination) != MA_SUCCESS || chunk == 0) break;
        memcpy(destination, samples + (size_t)written * channels, (size_t)chunk * channels * sizeof(float));
        ma_pcm_rb_commit_write(&audio.captureBuffer, chunk);
        written += chunk;
    }
    // If the buffer was full, the rest is dropped: the main thread stopped reading, old audio is useless anyway

    // The instrument being heard: its inputs mixed down into the monitor buffer, a chunk at a time
    if (!audio.monitorGate) return;
    // How much the input hands over at once: the monitor keeps that much waiting, and no more (see monitorRead)
    ma_uint32 burst = audio.captureBurst.load(std::memory_order_relaxed);
    audio.captureBurst.store(frameCount > burst ? frameCount : burst - (burst - frameCount) / 64, std::memory_order_relaxed);
    const ma_uint32 mixed = std::min<ma_uint32>(channels, MAX_MONITOR_INPUTS);
    for (ma_uint32 done = 0; done < frameCount;){
        ma_uint32 chunk = std::min(frameCount - done, MONITOR_CHUNK);
        for (ma_uint32 i = 0; i < chunk; i++){
            const float* frame = samples + (size_t)(done + i) * channels;
            float sum = 0.0f;
            for (ma_uint32 c = 0; c < mixed; c++) sum += frame[c] * audio.monitorWeights[c].load(std::memory_order_relaxed);
            audio.monitorScratch[i] = sum;
        }
        for (ma_uint32 put = 0; put < chunk;){
            ma_uint32 space = chunk - put;
            void* destination;
            if (ma_pcm_rb_acquire_write(&audio.monitorBuffer, &space, &destination) != MA_SUCCESS || space == 0) break;
            memcpy(destination, audio.monitorScratch + put, space * sizeof(float));
            ma_pcm_rb_commit_write(&audio.monitorBuffer, space);
            put += space;
        }
        done += chunk;
    }
}

static void captureCallback(ma_device* device, void* output, const void* input, ma_uint32 frameCount){
    (void)device;
    (void)output;
    writeCapture((const float*)input, frameCount);
}

static void asioCallback(const float* frames, int frameCount){
    audio.directInput = frames; // for renderOutput, next in the same call
    audio.directInputFrames = frameCount;
    if (audio.asioGate) writeCapture(frames, (ma_uint32)frameCount);
}

// The tone the main thread set last, taken up by an audio thread between two of its buffers. Never waits: a tone
// being written just then is taken at the next buffer.
static void takeTone(ToneRunner& runner, int sampleRate){
    setToneChainRate(runner.chain, sampleRate);
    unsigned version = audio.toneVersion.load(std::memory_order_acquire);
    if (version == runner.seen || (version & 1u)) return;
    ToneParameters parameters;
    std::memcpy((void*)&parameters, (const void*)&audio.toneShared, sizeof parameters);
    std::atomic_thread_fence(std::memory_order_acquire);
    if (audio.toneVersion.load(std::memory_order_relaxed) != version) return; // rewritten while copied
    setToneChain(runner.chain, parameters);
    runner.seen = version;
}

// The outputs, in the same call as the inputs (the ASIO driver's or the duplex device's): everything lahn plays, and
// the instrument heard straight through (the real sound, not the synth), from the input just handed over: one buffer
// in, one out, as Ableton does
static void renderOutput(float* stereo, int frameCount){
    if (audio.engineExternal.load(std::memory_order_acquire)) ma_engine_read_pcm_frames(&audio.engine, stereo, (ma_uint64)frameCount, nullptr);
    if (!audio.monitorGate || audio.monitorSynth || !audio.directInput || audio.directInputFrames != frameCount) return;
    const ma_uint32 channels = audio.captureChannels, mixed = std::min<ma_uint32>(channels, MAX_MONITOR_INPUTS);
    takeTone(audio.directTone, (int)audio.directRate.load(std::memory_order_relaxed));
    for (int done = 0; done < frameCount;){
        int chunk = std::min(frameCount - done, (int)MONITOR_CHUNK);
        for (int i = 0; i < chunk; i++){
            const float* frame = audio.directInput + (size_t)(done + i) * channels;
            float sum = 0.0f;
            for (ma_uint32 c = 0; c < mixed; c++) sum += frame[c] * audio.monitorWeights[c].load(std::memory_order_relaxed);
            audio.monitorScratch[i] = sum;
        }
        processToneChain(audio.directTone.chain, audio.monitorScratch, chunk);
        for (int i = 0; i < chunk; i++){
            stereo[2 * (done + i)] += audio.monitorScratch[i];
            stereo[2 * (done + i) + 1] += audio.monitorScratch[i];
        }
        done += chunk;
    }
}

// ASIO: the interface's own driver, straight to the hardware (audio/asiodriver). Its inputs fill the same buffer as
// Windows' would, so everything reading the input works the same. And lahn plays through it too, when it can: the
// engine is started again without a device, for the driver's callback to read, which takes the output from Windows'
// 20-30 ms to one of the driver's buffers. Not while a song is loaded (the engine starting again would stop it): then
// the driver is for listening, and the output stays with Windows.
static bool startAsioCapture(const std::string& device, std::string& error){
    std::string driver = device.substr(std::strlen(ASIO_PREFIX));
    const bool duplex = !audio.songReady;
    const std::string outputDevice = audio.outputDeviceWanted;
    const float volume = audio.engineReady ? ma_engine_get_volume(&audio.engine) : 1.0f;
    // At the output's rate: one interface asked for two rates at once would glitch or refuse
    double rate = audio.engineReady ? (double)ma_engine_get_sample_rate(&audio.engine) : 48000.0;
    if (duplex) stopEngine(); // the driver may need the device Windows' output has (ASIO4ALL does)
    audio.directInput = nullptr;
    clearToneChain(audio.directTone.chain); // the callback isn't running yet
    audio.directTone.seen = ~0u;
    if (!startAsio(driver, rate, asioCallback, duplex ? renderOutput : nullptr, error)){
        std::string ignored;
        if (duplex) startEngine(outputDevice, ignored);
        return false;
    }
    std::string engineError;
    if (duplex && !(asioOutputChannels() > 0 && startEngineExternal((ma_uint32)asioSampleRate(), engineError))){
        startEngine(outputDevice, engineError); // no outputs to play through: Windows' again
    }
    if (audio.engineReady) ma_engine_set_volume(&audio.engine, volume);
    audio.captureChannels = (ma_uint32)std::max(1, asioInputChannels());
    if (ma_pcm_rb_init(ma_format_f32, audio.captureChannels, CAPTURE_BUFFER_FRAMES, nullptr, nullptr, &audio.captureBuffer) != MA_SUCCESS){
        audio.engineExternal = false;
        stopAsio();
        stopEngine();
        std::string ignored;
        startEngine(outputDevice, ignored);
        error = "could not create capture buffer";
        return false;
    }
    audio.asioDevice = device;
    audio.captureExclusive = true; // nothing stands between the driver and lahn
    audio.captureReady = true;
    audio.asioGate = true;
    return true;
}

#ifdef _WIN32
// A duplex device's call: the input just heard, and the output to fill
static void duplexCallback(ma_device* device, void* output, const void* input, ma_uint32 frameCount){
    (void)device;
    audio.duplexCalls.fetch_add(1, std::memory_order_relaxed);
    audio.directInput = (const float*)input;
    audio.directInputFrames = (int)frameCount;
    if (audio.asioGate) writeCapture((const float*)input, frameCount);
    renderOutput((float*)output, (int)frameCount);
}

// Windows' own devices, both ways at once: the input device and the output device as one duplex device, in
// Windows' low-latency shared mode where the device allows it (periods of a few ms instead of 10), the engine read in
// its callback, the instrument heard straight through, as with ASIO. The output stays shared (other programs still
// play); the input is lahn's alone when that's asked (skipping Windows' effects). Not while a song is loaded: starting
// the output again would stop it. False when it can't be opened: separate devices then, as before.
static bool startDuplexCapture(const std::string& inputDevice, std::string& error){
    const std::string outputDevice = audio.outputDeviceWanted;
    const float volume = audio.engineReady ? ma_engine_get_volume(&audio.engine) : 1.0f;
    stopEngine();
    ma_device_config config = ma_device_config_init(ma_device_type_duplex);
    config.capture.format = ma_format_f32;
    config.capture.channels = 0;          // every input the device has
    config.playback.format = ma_format_f32;
    config.playback.channels = 2;
    config.sampleRate = 0;                // the output's native rate
    config.periodSizeInFrames = LOW_LATENCY_PERIOD_FRAMES;
    config.performanceProfile = ma_performance_profile_low_latency;
    config.wasapi.noAutoConvertSRC = MA_TRUE; // miniaudio converts rates itself, which Windows' low-latency mode needs
    config.dataCallback = duplexCallback;
    ma_device_id inputId, outputId;
    if (findDevice(ma_device_type_capture, inputDevice, inputId)) config.capture.pDeviceID = &inputId;
    if (findDevice(ma_device_type_playback, outputDevice, outputId)) config.playback.pDeviceID = &outputId;
    ma_result result = MA_ERROR;
    audio.captureExclusive = false;
#ifdef _WIN32
    if (audio.exclusiveWanted){
        config.capture.shareMode = ma_share_mode_exclusive;
        result = ma_device_init(&audio.context, &config, &audio.duplexDevice);
        audio.captureExclusive = result == MA_SUCCESS;
        config.capture.shareMode = ma_share_mode_shared;
    }
#endif
    if (result != MA_SUCCESS) result = ma_device_init(&audio.context, &config, &audio.duplexDevice);
    auto fallBack = [&](const std::string& why){
        std::string ignored;
        startEngine(outputDevice, ignored);
        if (audio.engineReady) ma_engine_set_volume(&audio.engine, volume);
        error = why;
        return false;
    };
    if (result != MA_SUCCESS) return fallBack(std::string("could not open input and output together: ") + ma_result_description(result));
    audio.captureChannels = std::max<ma_uint32>(1, audio.duplexDevice.capture.channels);
    if (ma_pcm_rb_init(ma_format_f32, audio.captureChannels, CAPTURE_BUFFER_FRAMES, nullptr, nullptr, &audio.captureBuffer) != MA_SUCCESS){
        ma_device_uninit(&audio.duplexDevice);
        return fallBack("could not create capture buffer");
    }
    std::string engineError;
    audio.directInput = nullptr;
    clearToneChain(audio.directTone.chain); // the callback isn't running yet
    audio.directTone.seen = ~0u;
    if (!startEngineExternal(audio.duplexDevice.sampleRate, engineError)){
        ma_device_uninit(&audio.duplexDevice);
        ma_pcm_rb_uninit(&audio.captureBuffer);
        return fallBack(engineError);
    }
    ma_engine_set_volume(&audio.engine, volume);
    audio.duplexReady = true;
    audio.captureReady = true;
    audio.asioGate = true;
    audio.duplexCalls = 0;
    if (ma_device_start(&audio.duplexDevice) != MA_SUCCESS){
        closeCapture(); // hands the output back to its own device
        error = "could not start input and output together";
        return false;
    }
    // Some systems open a duplex device that never calls (seen with PulseAudio): nothing would play at all. It must
    // call within DUPLEX_START_S, or it's closed and separate devices are used.
    const auto started = std::chrono::steady_clock::now();
    while (audio.duplexCalls.load() == 0 && std::chrono::steady_clock::now() - started < std::chrono::duration<double>(DUPLEX_START_S)){
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    if (audio.duplexCalls.load() == 0){
        closeCapture();
        error = "input and output together never started";
        return false;
    }
    return true;
}
#endif

// --- Hearing the instrument ---

// The engine's audio thread asks for the instrument's sound: whatever the capture thread put in, through the amp.
// The input hands over its samples in bursts (a few ms apart through ASIO, tens through Windows' shared mode), so
// what waits must bridge one burst and one read, or reads come up short and it crackles. Anything waiting beyond that
// is old (the devices' clocks drift apart, something stalled) and is skipped rather than heard late. So the delay is
// the least the input allows, on any device. With nothing waiting, silence.
static ma_result monitorRead(ma_data_source* source, void* out, ma_uint64 frameCount, ma_uint64* framesRead){
    MonitorSource* self = (MonitorSource*)source;
    float* samples = (float*)out;
    const ma_uint32 wanted = (ma_uint32)frameCount;
    if (audio.monitorSynth.load(std::memory_order_relaxed) || audio.engineExternal.load(std::memory_order_relaxed)){
        // Heard as a synth: the main thread reads the input (readMonitor) and plays the notes it finds. Or through
        // ASIO: the driver's own callback plays it straight through (renderOutput).
        std::fill(samples, samples + wanted, 0.0f);
        *framesRead = frameCount;
        return MA_SUCCESS;
    }
    ma_uint32 waiting = ma_pcm_rb_available_read(&audio.monitorBuffer);
    const ma_uint32 cushion = audio.captureBurst.load(std::memory_order_relaxed) + wanted;
    if (waiting > cushion + wanted) ma_pcm_rb_seek_read(&audio.monitorBuffer, waiting - cushion);
    ma_uint32 done = 0;
    while (done < wanted){
        ma_uint32 chunk = wanted - done;
        void* from;
        if (ma_pcm_rb_acquire_read(&audio.monitorBuffer, &chunk, &from) != MA_SUCCESS || chunk == 0) break;
        memcpy(samples + done, from, chunk * sizeof(float));
        ma_pcm_rb_commit_read(&audio.monitorBuffer, chunk);
        done += chunk;
    }
    std::fill(samples + done, samples + wanted, 0.0f);
    takeTone(audio.engineTone, (int)self->sampleRate);
    processToneChain(audio.engineTone.chain, samples, (int)wanted);
    *framesRead = frameCount;
    return MA_SUCCESS;
}

static ma_result monitorSeek(ma_data_source*, ma_uint64){ return MA_SUCCESS; } // live: there's nowhere else to be

static ma_result monitorFormat(ma_data_source* source, ma_format* format, ma_uint32* channels, ma_uint32* sampleRate,
                               ma_channel* channelMap, size_t channelMapCapacity){
    *format = ma_format_f32;
    *channels = 1;
    *sampleRate = ((MonitorSource*)source)->sampleRate;
    ma_channel_map_init_standard(ma_standard_channel_map_default, channelMap, channelMapCapacity, 1);
    return MA_SUCCESS;
}

static ma_result monitorCursor(ma_data_source*, ma_uint64* cursor){
    *cursor = 0;
    return MA_SUCCESS;
}

static ma_result monitorLength(ma_data_source*, ma_uint64*){ return MA_NOT_IMPLEMENTED; } // it never ends

static ma_data_source_vtable MONITOR_VTABLE = { monitorRead, monitorSeek, monitorFormat, monitorCursor, monitorLength, nullptr, 0 };

static ma_uint32 openCaptureRate(){
    if (!audio.captureReady) return 0;
    if (audio.duplexReady) return audio.duplexDevice.sampleRate; // the input converted to the device's rate
    return audio.asioDevice.empty() ? audio.captureDevice.sampleRate : (ma_uint32)asioSampleRate();
}

// Which of the capture's inputs are heard: the ones asked for, or every one but the excluded (the voice's mic)
static void updateMonitorWeights(){
    for (int c = 0; c < MAX_MONITOR_INPUTS; c++){
        bool heard = audio.monitorInputs.empty() ? c != audio.monitorExcluded
                                                 : std::count(audio.monitorInputs.begin(), audio.monitorInputs.end(), c) > 0;
        audio.monitorWeights[c].store(heard && c < (int)audio.captureChannels ? 1.0f : 0.0f, std::memory_order_relaxed);
    }
}

static void startMonitorSound(){
    if (audio.monitorSoundReady || !audio.engineReady || !audio.monitorBufferReady || !audio.captureReady) return;
    ma_data_source_config config = ma_data_source_config_init();
    config.vtable = &MONITOR_VTABLE;
    if (ma_data_source_init(&config, &audio.monitorSource.base) != MA_SUCCESS) return;
    audio.monitorSource.sampleRate = openCaptureRate(); // the engine resamples if its own rate differs
    clearToneChain(audio.engineTone.chain); // the engine doesn't read the source yet
    audio.engineTone.seen = ~0u;
    if (ma_sound_init_from_data_source(&audio.engine, &audio.monitorSource, MA_SOUND_FLAG_NO_SPATIALIZATION, nullptr, &audio.monitorSound) != MA_SUCCESS){
        ma_data_source_uninit(&audio.monitorSource.base);
        return;
    }
    ma_sound_start(&audio.monitorSound);
    audio.monitorSoundReady = true;
}

static void stopMonitorSound(){
    if (!audio.monitorSoundReady) return;
    ma_sound_uninit(&audio.monitorSound); // stops the engine reading from it first
    ma_data_source_uninit(&audio.monitorSource.base);
    audio.monitorSoundReady = false;
}

// The capture was just opened (maybe with other inputs, at another rate): the monitor follows it
static void monitorFollowCapture(){
    if (!audio.monitorWanted) return;
    updateMonitorWeights();
    if (audio.monitorSoundReady && audio.monitorSource.sampleRate != openCaptureRate()) stopMonitorSound();
    startMonitorSound();
    audio.monitorGate = true;
}

bool setMonitor(bool on, const std::string& inputDevice, const std::vector<int>& inputs, int excluded, std::string& error){
    audio.monitorWanted = on;
    audio.monitorDevice = inputDevice;
    audio.monitorInputs = inputs;
    audio.monitorExcluded = excluded;
    if (!on){
        audio.monitorGate = false;
        stopMonitorSound();
        if (!audio.captureReader) closeCapture();
        return true;
    }
    if (!audio.contextReady){
        error = "audio is not running";
        return false;
    }
    if (!audio.monitorBufferReady){
        if (ma_pcm_rb_init(ma_format_f32, 1, MONITOR_BUFFER_FRAMES, nullptr, nullptr, &audio.monitorBuffer) != MA_SUCCESS){
            error = "could not create the monitor's buffer";
            return false;
        }
        audio.monitorBufferReady = true;
    }
    // Opened already for a screen reading it: heard from there (it goes back to the monitor's device when the screen
    // stops: reopening under a reader could change its number of inputs)
    if (!audio.captureReady || (!audio.captureReader && (audio.captureOpenedFor != inputDevice || audio.captureStale))){
        closeCapture();
        if (!openCapture(inputDevice, error)) return false;
    }
    monitorFollowCapture();
    return true;
}

void setMonitorTone(const ToneParameters& tone){
    unsigned version = audio.toneVersion.load(std::memory_order_relaxed);
    audio.toneVersion.store(version + 1, std::memory_order_relaxed); // odd: being written
    std::atomic_thread_fence(std::memory_order_release);
    std::memcpy((void*)&audio.toneShared, (const void*)&tone, sizeof tone);
    audio.toneVersion.store(version + 2, std::memory_order_release);
}

bool monitorActive(){
    return audio.monitorWanted && audio.monitorSoundReady && audio.monitorGate;
}

void setMonitorSynth(bool synth){
    if (synth && !audio.monitorSynth && audio.monitorBufferReady){
        // What waited for the speakers is old news for the note finder: it starts from now
        ma_uint32 waiting = ma_pcm_rb_available_read(&audio.monitorBuffer);
        if (waiting > 0) ma_pcm_rb_seek_read(&audio.monitorBuffer, waiting);
    }
    audio.monitorSynth = synth;
    if (!synth) releaseSynthNote();
}

int readMonitor(float* out, int maxFrames){
    if (!audio.monitorBufferReady || !audio.monitorSynth) return 0;
    int total = 0;
    while (total < maxFrames){
        ma_uint32 chunk = (ma_uint32)(maxFrames - total);
        void* from;
        if (ma_pcm_rb_acquire_read(&audio.monitorBuffer, &chunk, &from) != MA_SUCCESS || chunk == 0) break;
        memcpy(out + total, from, chunk * sizeof(float));
        ma_pcm_rb_commit_read(&audio.monitorBuffer, chunk);
        total += (int)chunk;
    }
    return total;
}

int monitorSampleRate(){
    return audio.monitorGate ? (int)openCaptureRate() : 0;
}

const double SYNTH_NOTE_S = 4.0;        // rendered this long: a note held longer has died away by then anyway
const ma_uint64 SYNTH_CROSSFADE_MS = 15; // the last note fading as the next starts: no click, no smear
const ma_uint64 SYNTH_RELEASE_MS = 80;   // a string muted

void playSynthNote(float frequency, float volume){
    if (!audio.engineReady) return;
    Voice& last = audio.synthVoices[audio.synthCurrent];
    if (last.ready) ma_sound_stop_with_fade_in_milliseconds(&last.sound, SYNTH_CROSSFADE_MS);
    audio.synthCurrent = 1 - audio.synthCurrent;
    Voice& voice = audio.synthVoices[audio.synthCurrent];
    releaseVoice(voice); // the note before last: long faded
    ma_uint32 sampleRate = ma_engine_get_sample_rate(&audio.engine);
    voice.samples.resize((size_t)(SYNTH_NOTE_S * sampleRate)); // keeps its memory: no allocation after the first notes
    renderBass(voice.samples.data(), (int)voice.samples.size(), frequency, (int)sampleRate);
    startVoice(voice, voice.samples.data(), voice.samples.size(), 1.0f, volume, ma_engine_get_time_in_pcm_frames(&audio.engine));
}

void releaseSynthNote(){
    Voice& voice = audio.synthVoices[audio.synthCurrent];
    if (voice.ready) ma_sound_stop_with_fade_in_milliseconds(&voice.sound, SYNTH_RELEASE_MS);
}

// Opens the input device and starts it filling the capture buffer (and the monitor's): nothing may be open
static bool openCapture(const std::string& inputDevice, std::string& error){
    audio.captureOpenedFor = inputDevice;
    audio.captureStale = false;
    if (inputDevice.rfind(ASIO_PREFIX, 0) == 0){
        if (!startAsioCapture(inputDevice, error)) return false;
        monitorFollowCapture();
        return true;
    }
#ifdef _WIN32
    // Windows: input and output as one low-latency device. Not elsewhere: PulseAudio opens one that never runs, and
    // leaves the audio it was opened alongside stuck; separate devices always work there.
    std::string duplexError;
    if (!audio.songReady && startDuplexCapture(inputDevice, duplexError)){
        monitorFollowCapture();
        return true;
    }
#endif

    // Every input the device has, as its own channel: an audio interface's guitar and microphone stay apart
    ma_device_config config = ma_device_config_init(ma_device_type_capture);
    config.capture.format = ma_format_f32;
    config.capture.channels = 0; // the device's own channel count
    config.sampleRate = 0;       // the device's native rate, so nothing gets resampled
    config.dataCallback = captureCallback;
    ma_device_id id;
    if (findDevice(ma_device_type_capture, inputDevice, id)) config.capture.pDeviceID = &id; // not found: system default
    ma_result result = MA_ERROR;
    audio.captureExclusive = false;
#ifdef _WIN32
    if (audio.exclusiveWanted){
        config.capture.shareMode = ma_share_mode_exclusive;
        result = ma_device_init(&audio.context, &config, &audio.captureDevice);
        audio.captureExclusive = result == MA_SUCCESS;
        config.capture.shareMode = ma_share_mode_shared; // if it failed: shared, below
    }
#endif
    if (result != MA_SUCCESS) result = ma_device_init(&audio.context, &config, &audio.captureDevice);
    if (result != MA_SUCCESS){
        error = std::string("could not open input device: ") + ma_result_description(result);
        return false;
    }
    audio.captureChannels = std::max<ma_uint32>(1, audio.captureDevice.capture.channels);
    // miniaudio's ring buffer is lock-free for exactly one writer thread and one reader thread; it's made once the
    // channel count is known, before the device starts writing to it
    result = ma_pcm_rb_init(ma_format_f32, audio.captureChannels, CAPTURE_BUFFER_FRAMES, nullptr, nullptr, &audio.captureBuffer);
    if (result != MA_SUCCESS){
        ma_device_uninit(&audio.captureDevice);
        error = std::string("could not create capture buffer: ") + ma_result_description(result);
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
    monitorFollowCapture();
    return true;
}

void setExclusiveCapture(bool on){
    if (on != audio.exclusiveWanted) audio.captureStale = true; // opened the other way: the next start reopens it
    audio.exclusiveWanted = on;
}

bool captureIsExclusive(){
    return audio.captureReady && audio.captureExclusive;
}

static void closeCapture(){
    if (!audio.captureReady) return;
    // The thread writing to the buffer is stopped before the buffer goes away
    const bool engineWasExternal = audio.engineExternal;
    if (!audio.asioDevice.empty()){
        audio.asioGate = false;
        audio.engineExternal = false;
        stopAsio();
        audio.asioDevice.clear();
    } else if (audio.duplexReady){
        audio.asioGate = false;
        audio.engineExternal = false;
        ma_device_uninit(&audio.duplexDevice); // stops its callback first
        audio.duplexReady = false;
    } else {
        ma_device_uninit(&audio.captureDevice);
    }
    ma_pcm_rb_uninit(&audio.captureBuffer);
    audio.captureReady = false;
    if (engineWasExternal && !audio.closing){
        // Everything was playing through that callback: back to the output device of its own
        float volume = ma_engine_get_volume(&audio.engine);
        stopEngine();
        std::string error;
        if (startEngine(audio.outputDeviceWanted, error)) ma_engine_set_volume(&audio.engine, volume);
    }
}

// Throws away what waited in the capture buffer with nobody reading: a screen starts from now
static void skipCaptureBacklog(){
    ma_uint32 waiting = ma_pcm_rb_available_read(&audio.captureBuffer);
    if (waiting > 0) ma_pcm_rb_seek_read(&audio.captureBuffer, waiting);
}

bool startCapture(const std::string& inputDevice, std::string& error){
    if (!audio.contextReady){
        error = "audio is not running";
        return false;
    }
    // Already open on this device (the monitor keeps it open): the screen reads from here on
    if (audio.captureReady && audio.captureOpenedFor == inputDevice && !audio.captureStale){
        skipCaptureBacklog();
        audio.captureReader = true;
        return true;
    }
    closeCapture();
    if (!openCapture(inputDevice, error)) return false;
    audio.captureReader = true;
    return true;
}

void stopCapture(){
    audio.captureReader = false;
    if (audio.engineExternal) return; // everything plays through that callback: the input stays open
    if (!audio.monitorWanted){
        closeCapture();
        return;
    }
    // The monitor keeps listening: on its own device, the way it should be opened
    if (audio.captureReady && audio.captureOpenedFor == audio.monitorDevice && !audio.captureStale) return;
    closeCapture();
    std::string error;
    openCapture(audio.monitorDevice, error);
}

// An ASIO driver whose settings changed (its buffer size, in its control panel) asks to be started again. Readers
// sized their buffers for its inputs, so if the number of inputs came back different it stays stopped rather than
// overrun them: they see no input, and whoever opens it again sizes for the new count.
static void restartAsioIfAsked(){
    if (audio.asioDevice.empty() || !asioRestartRequested()) return;
    std::string device = audio.asioDevice, error;
    ma_uint32 channels = audio.captureChannels;
    closeCapture();
    if (openCapture(device, error) && audio.captureReader && audio.captureChannels != channels) closeCapture();
}

int captureSampleRate(){
    return (int)openCaptureRate();
}

const char* captureDeviceName(){
    if (!audio.captureReady) return "none";
    if (audio.duplexReady) return audio.duplexDevice.capture.name;
    return audio.asioDevice.empty() ? audio.captureDevice.capture.name : audio.asioDevice.c_str();
}

bool captureIsAsio(){
    return audio.captureReady && !audio.asioDevice.empty();
}

double captureLatencySeconds(){
    if (!audio.captureReady) return 0.0;
    if (!audio.asioDevice.empty()) return asioInputLatencyFrames() / std::max(1.0, asioSampleRate());
    const ma_device& device = audio.duplexReady ? audio.duplexDevice : audio.captureDevice;
    return (double)device.capture.internalPeriodSizeInFrames * device.capture.internalPeriods / std::max<ma_uint32>(1, device.capture.internalSampleRate);
}

void openInputDriverSettings(){
    if (captureIsAsio()) openAsioControlPanel();
}

int captureChannels(){
    return audio.captureReady ? (int)audio.captureChannels : 0;
}

int readCapture(float* out, int maxFrames, int channel){
    restartAsioIfAsked();
    if (!audio.captureReady) return 0;
    const int channels = (int)audio.captureChannels;
    int total = 0;
    while (total < maxFrames){
        ma_uint32 chunk = (ma_uint32)(maxFrames - total);
        void* source;
        if (ma_pcm_rb_acquire_read(&audio.captureBuffer, &chunk, &source) != MA_SUCCESS || chunk == 0) break;
        takeChannel((const float*)source, (int)chunk, channels, channel, out + total); // the one input asked for
        ma_pcm_rb_commit_read(&audio.captureBuffer, chunk);
        total += (int)chunk;
    }
    return total;
}

int readCaptureAll(float* out, int maxFrames){
    restartAsioIfAsked();
    if (!audio.captureReady) return 0;
    const int channels = (int)audio.captureChannels;
    int total = 0;
    while (total < maxFrames){
        ma_uint32 chunk = (ma_uint32)(maxFrames - total);
        void* source;
        if (ma_pcm_rb_acquire_read(&audio.captureBuffer, &chunk, &source) != MA_SUCCESS || chunk == 0) break;
        memcpy(out + (size_t)total * channels, source, (size_t)chunk * channels * sizeof(float));
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

// Busy = sounding now, or scheduled to start later. ma_sound_is_playing alone isn't enough: it answers
// for the current moment, so a note scheduled for later looks free until it starts.
static bool voiceBusy(Voice& voice){
    if (!voice.ready) return false;
    return ma_sound_is_playing(&voice.sound) || ma_engine_get_time_in_pcm_frames(&audio.engine) < voice.startFrame;
}

void stopPreviews(){
    for (Voice& voice : audio.voices) releaseVoice(voice);
}

// A free voice, or else the one that has been sounding longest. Comparing start times on the audio clock
// matters: a sound scheduled for later has the latest start, so it's never cut before it plays.
static Voice& takeVoice(){
    Voice* voice = &audio.voices[0];
    for (Voice& candidate : audio.voices){
        if (!voiceBusy(candidate)){
            voice = &candidate;
            break;
        }
        if (candidate.startFrame < voice->startFrame) voice = &candidate;
    }
    releaseVoice(*voice);
    return *voice;
}

// Plays samples through a voice from `startFrame` on the engine clock (or now, if that has passed).
// Scheduled on the engine's own clock, counted in samples: the sound starts on exactly the right sample,
// however late this frame runs. Starting it from the game loop instead would be up to a frame late.
static void startVoice(Voice& voice, const float* data, size_t frames, float pitchRatio, float volume, unsigned long long startFrame){
    ma_uint32 sampleRate = ma_engine_get_sample_rate(&audio.engine);
    ma_audio_buffer_config config = ma_audio_buffer_config_init(ma_format_f32, 1, frames, data, nullptr);
    config.sampleRate = sampleRate;
    if (ma_audio_buffer_init(&config, &voice.buffer) != MA_SUCCESS) return;
    if (ma_sound_init_from_data_source(&audio.engine, &voice.buffer, MA_SOUND_FLAG_NO_SPATIALIZATION, nullptr, &voice.sound) != MA_SUCCESS){
        ma_audio_buffer_uninit(&voice.buffer);
        return;
    }
    voice.ready = true;
    if (pitchRatio != 1.0f) ma_sound_set_pitch(&voice.sound, pitchRatio); // 2.0 = an octave up (and twice as short)
    ma_sound_set_volume(&voice.sound, volume);
    ma_uint64 now = ma_engine_get_time_in_pcm_frames(&audio.engine);
    voice.startFrame = std::max(startFrame, now);
    if (voice.startFrame > now) ma_sound_set_start_time_in_pcm_frames(&voice.sound, voice.startFrame);
    ma_sound_start(&voice.sound);
}

void playPreview(float frequency, float delaySeconds){
    if (!audio.engineReady) return;
    ma_uint32 sampleRate = ma_engine_get_sample_rate(&audio.engine);
    startPreview(frequency, ma_engine_get_time_in_pcm_frames(&audio.engine) + (ma_uint64)(delaySeconds * sampleRate));
}

void playPreviewAt(float frequency, double time){
    if (!audio.engineReady) return;
    startPreview(frequency, (ma_uint64)std::llround(std::max(0.0, time) * ma_engine_get_sample_rate(&audio.engine)));
}

const float MAX_STRING_NOTE_S = 4.0f;

static void startStringNote(float frequency, bool bass, float seconds, ma_uint64 startFrame, float volume){
    Voice& voice = takeVoice();
    ma_uint32 sampleRate = ma_engine_get_sample_rate(&audio.engine);
    voice.samples.resize((size_t)(std::clamp(seconds, 0.05f, MAX_STRING_NOTE_S) * sampleRate));
    renderStringNote(voice.samples.data(), (int)voice.samples.size(), frequency, (int)sampleRate, bass ? StringVoice::Bass : StringVoice::Guitar,
                     (unsigned)audio.previewCount++);
    startVoice(voice, voice.samples.data(), voice.samples.size(), 1.0f, std::clamp(volume, 0.0f, 1.0f), startFrame);
}

void playStringNote(float frequency, bool bass, float seconds, float volume){
    if (!audio.engineReady) return;
    startStringNote(frequency, bass, seconds, ma_engine_get_time_in_pcm_frames(&audio.engine), volume);
}

void playStringNoteAt(float frequency, bool bass, float seconds, double time, float volume){
    if (!audio.engineReady) return;
    startStringNote(frequency, bass, seconds, (ma_uint64)std::llround(std::max(0.0, time) * ma_engine_get_sample_rate(&audio.engine)), volume);
}

// A preview note starting at a frame of the engine's clock
static void startPreview(float frequency, ma_uint64 startFrame, const char* builtIn){
    Voice& voice = takeVoice();

    // Built-in sounds are rendered at the engine's own sample rate and pitch, so nothing is resampled.
    // A custom sound plays from the shared decoded copy, re-pitched to the note if it has a pitch.
    ma_uint32 sampleRate = ma_engine_get_sample_rate(&audio.engine);
    const float* data;
    size_t frames;
    float pitchRatio = 1.0f;
    if (builtIn || audio.customSound.empty()){
        voice.samples.resize((size_t)(PREVIEW_LENGTH_S * sampleRate)); // keeps its memory between notes: no allocation after the first
        renderBuiltInSound(builtIn ? builtIn : audio.previewSoundName.c_str(), voice.samples.data(), (int)voice.samples.size(), frequency,
                           (int)sampleRate, (unsigned)audio.previewCount);
        data = voice.samples.data();
        frames = voice.samples.size();
    } else {
        data = audio.customSound.data();
        frames = audio.customSound.size();
        if (audio.customSoundRoot > 0.0f) pitchRatio = frequency / audio.customSoundRoot;
    }
    audio.previewCount++;
    startVoice(voice, data, frames, pitchRatio, audio.previewVolume, startFrame);
}

void playBuiltInNote(const char* name, float frequency, float volume){
    if (!audio.engineReady || volume <= 0.0f) return;
    Voice& voice = takeVoice();
    ma_uint32 sampleRate = ma_engine_get_sample_rate(&audio.engine);
    voice.samples.resize((size_t)(PREVIEW_LENGTH_S * sampleRate));
    renderBuiltInSound(name, voice.samples.data(), (int)voice.samples.size(), frequency, (int)sampleRate, (unsigned)audio.previewCount++);
    startVoice(voice, voice.samples.data(), voice.samples.size(), 1.0f, volume, ma_engine_get_time_in_pcm_frames(&audio.engine));
}

void playKeysNote(float frequency){
    if (!audio.engineReady) return;
    startPreview(frequency, ma_engine_get_time_in_pcm_frames(&audio.engine), "keys");
}

void setHitSoundVolume(float volume){
    audio.hitSoundVolume = std::clamp(volume, 0.0f, 1.0f);
}

// The hit sound: the built-in drop, high above anything played, so it reads as "got it" rather than as a note
const float HIT_SOUND_PERFECT_HZ = 1318.5f; // E6
const float HIT_SOUND_GOOD_HZ = 1046.5f;    // C6: a little duller

void playHitSound(bool perfect){
    if (!audio.engineReady || audio.hitSoundVolume <= 0.0f) return;
    Voice& voice = takeVoice();
    ma_uint32 sampleRate = ma_engine_get_sample_rate(&audio.engine);
    voice.samples.resize((size_t)(0.25 * sampleRate)); // keeps its memory between hits
    renderBuiltInSound("drop", voice.samples.data(), (int)voice.samples.size(), perfect ? HIT_SOUND_PERFECT_HZ : HIT_SOUND_GOOD_HZ,
                       (int)sampleRate, (unsigned)audio.previewCount++);
    startVoice(voice, voice.samples.data(), voice.samples.size(), 1.0f, audio.hitSoundVolume, ma_engine_get_time_in_pcm_frames(&audio.engine));
}

void playDrum(bool high){
    if (!audio.engineReady) return;
    Voice& voice = takeVoice();
    ma_uint32 sampleRate = ma_engine_get_sample_rate(&audio.engine);
    voice.samples.resize((size_t)(0.5 * sampleRate));
    renderDrum(voice.samples.data(), (int)voice.samples.size(), (int)sampleRate, high);
    startVoice(voice, voice.samples.data(), voice.samples.size(), 1.0f, audio.previewVolume, ma_engine_get_time_in_pcm_frames(&audio.engine));
}

void playClickAt(double time, bool accent){
    if (!audio.engineReady) return;
    Voice& voice = takeVoice();
    ma_uint32 sampleRate = ma_engine_get_sample_rate(&audio.engine);
    voice.samples.resize((size_t)(CLICK_LENGTH_S * sampleRate));
    renderClick(voice.samples.data(), (int)voice.samples.size(), (int)sampleRate, accent);
    startVoice(voice, voice.samples.data(), voice.samples.size(), 1.0f, CLICK_VOLUME, (ma_uint64)std::llround(time * sampleRate));
}

double audioTime(){
    if (!audio.engineReady) return 0.0;
    double engineTime = (double)ma_engine_get_time_in_pcm_frames(&audio.engine) / ma_engine_get_sample_rate(&audio.engine);
    double now = wallClockSeconds();
    if (!audio.engineClockStarted){
        audio.engineSmoothTime = engineTime;
        audio.engineLastWallTime = now;
        audio.engineClockStarted = true;
        return engineTime;
    }
    // Same smoothing as songPosition: advance with real time, then close part of the gap to the engine's count
    double advance = now - audio.engineLastWallTime;
    double previous = audio.engineSmoothTime;
    audio.engineSmoothTime += advance;
    audio.engineLastWallTime = now;
    double drift = engineTime - audio.engineSmoothTime;
    if (drift > SNAP_THRESHOLD_S) audio.engineSmoothTime = engineTime;
    else if (drift < -SNAP_THRESHOLD_S) audio.engineSmoothTime -= advance; // behind (device starting up): wait, never run backwards
    else audio.engineSmoothTime += drift * DRIFT_CORRECTION;
    audio.engineSmoothTime = std::max(audio.engineSmoothTime, previous); // a small correction must not step it back either
    return audio.engineSmoothTime;
}

bool decodeAudioFile(const std::string& path, int sampleRate, int channels, std::vector<float>& out, std::string& error,
                     const std::atomic<bool>& cancel){
    // Floats at the rate and channels asked for: the decoder mixes the channels and converts the rate
    channels = std::clamp(channels, 1, 2);
    ma_decoder_config config = ma_decoder_config_init(ma_format_f32, (ma_uint32)channels, (ma_uint32)sampleRate);
    ma_decoder decoder;
    ma_result result = ma_decoder_init_file(path.c_str(), &config, &decoder);
    if (result != MA_SUCCESS){
        error = std::string("can't read it as audio: ") + ma_result_description(result);
        return false;
    }
    out.clear();
    std::vector<float> chunk(16384);
    for (;;){
        if (cancel) break;
        ma_uint64 read = 0;
        ma_decoder_read_pcm_frames(&decoder, chunk.data(), chunk.size() / channels, &read);
        if (read == 0) break;
        out.insert(out.end(), chunk.begin(), chunk.begin() + (size_t)read * channels);
    }
    ma_decoder_uninit(&decoder);
    if (cancel) return false;
    if (out.empty()){
        error = "there's no sound in it";
        return false;
    }
    return true;
}

bool songPeaks(const std::string& path, int peaksPerSecond, std::vector<float>& out, const std::atomic<bool>& cancel){
    // Mono floats at the file's own rate: the decoder mixes the channels down
    ma_decoder_config config = ma_decoder_config_init(ma_format_f32, 1, 0);
    ma_decoder decoder;
    if (ma_decoder_init_file(path.c_str(), &config, &decoder) != MA_SUCCESS) return false;
    const ma_uint64 framesPerPeak = std::max<ma_uint64>(1, decoder.outputSampleRate / (ma_uint32)peaksPerSecond);
    out.clear();
    std::vector<float> chunk(4096);
    float peak = 0.0f;
    ma_uint64 inPeak = 0;
    for (;;){
        if (cancel) break;
        ma_uint64 read = 0;
        ma_decoder_read_pcm_frames(&decoder, chunk.data(), chunk.size(), &read);
        if (read == 0) break;
        for (ma_uint64 i = 0; i < read; i++){
            peak = std::max(peak, std::fabs(chunk[i]));
            if (++inPeak == framesPerPeak){
                out.push_back(std::min(peak, 1.0f));
                peak = 0.0f;
                inPeak = 0;
            }
        }
    }
    ma_decoder_uninit(&decoder);
    return !cancel;
}
