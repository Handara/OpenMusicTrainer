#include "audio/asioinput.h"

#ifdef _WIN32

#include "core/sampleformat.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <objbase.h>

#include "asiosys.h"
#include "asio.h"
#include "asiodrivers.h"

#include <atomic>
#include <cstring>
#include <future>
#include <thread>

// From the SDK's host helpers (asiodrivers.cpp): the one driver loaded, and loading it
extern AsioDrivers* asioDrivers;
bool loadAsioDriver(char* name);

// What the driver's thread is told by the main thread
const UINT STOP_MESSAGE = WM_APP + 1;
const UINT PANEL_MESSAGE = WM_APP + 2;

static struct {
    std::thread thread;
    DWORD threadId = 0;
    std::atomic<bool> active{false};
    std::atomic<bool> restartRequested{false};

    // Set up before the driver starts calling, read on its thread
    AsioSink sink = nullptr;
    int channels = 0;
    long bufferFrames = 0;
    double sampleRate = 0.0;
    long inputLatency = 0;
    std::vector<ASIOBufferInfo> buffers;
    std::vector<SampleFormat> formats;
    std::vector<float> frames; // one buffer's worth, interleaved: made before the driver starts, never reallocated
    ASIOCallbacks callbacks{};
} asio;

// The driver's audio callback: every input's buffer into its place among interleaved frames, then out. Runs on the
// driver's own thread, every millisecond or two: no allocation, no locks.
static void bufferSwitch(long index, ASIOBool){
    const int channels = asio.channels, frames = (int)asio.bufferFrames;
    for (int c = 0; c < channels; c++){
        convertSamples(asio.buffers[c].buffers[index], asio.formats[c], frames, asio.frames.data() + c, channels);
    }
    asio.sink(asio.frames.data(), frames);
}

static ASIOTime* bufferSwitchTimeInfo(ASIOTime*, long index, ASIOBool processNow){
    bufferSwitch(index, processNow);
    return nullptr;
}

static void sampleRateDidChange(ASIOSampleRate){
    asio.restartRequested = true; // everything downstream was set up for the old rate
}

static long asioMessage(long selector, long value, void*, double*){
    switch (selector){
        case kAsioSelectorSupported:
            return value == kAsioEngineVersion || value == kAsioResetRequest || value == kAsioResyncRequest ||
                   value == kAsioLatenciesChanged || value == kAsioSupportsTimeInfo || value == kAsioSupportsTimeCode;
        case kAsioEngineVersion: return 2;
        case kAsioResetRequest: asio.restartRequested = true; return 1; // its settings changed: start it again
        case kAsioResyncRequest: return 1;    // a dropout it recovered from: nothing to redo
        case kAsioLatenciesChanged: return 1;
        case kAsioSupportsTimeInfo: return 0; // plain bufferSwitch is all that's needed
        case kAsioSupportsTimeCode: return 0;
    }
    return 0;
}

static bool formatOf(ASIOSampleType type, SampleFormat& format){
    switch (type){
        case ASIOSTInt16LSB:   format = SampleFormat::Int16; return true;
        case ASIOSTInt24LSB:   format = SampleFormat::Int24; return true;
        case ASIOSTInt32LSB:   format = SampleFormat::Int32; return true;
        case ASIOSTFloat32LSB: format = SampleFormat::Float32; return true;
        case ASIOSTFloat64LSB: format = SampleFormat::Float64; return true;
        case ASIOSTInt32LSB16: format = SampleFormat::Int32In16; return true;
        case ASIOSTInt32LSB18: format = SampleFormat::Int32In18; return true;
        case ASIOSTInt32LSB20: format = SampleFormat::Int32In20; return true;
        case ASIOSTInt32LSB24: format = SampleFormat::Int32In24; return true;
        default: return false; // big-endian and DSD formats: never on a PC
    }
}

// Loads, sets up and starts the driver; on the driver's thread. An empty string when it's running.
static std::string openDriver(const std::string& driver, double wantedRate, HWND window){
    char name[128] = {};
    std::strncpy(name, driver.c_str(), sizeof(name) - 1);
    if (!loadAsioDriver(name)) return "couldn't load the ASIO driver '" + driver + "'";
    ASIODriverInfo info{};
    info.asioVersion = 2;
    info.sysRef = window;
    if (ASIOInit(&info) != ASE_OK) return "the ASIO driver '" + driver + "' didn't start: " + info.errorMessage;

    // Everything after ASIOInit must be undone by ASIOExit if it fails
    auto fail = [](const std::string& why){ ASIOExit(); return why; };
    long inputs = 0, outputs = 0;
    if (ASIOGetChannels(&inputs, &outputs) != ASE_OK || inputs < 1) return fail("the ASIO driver has no inputs");
    long minSize = 0, maxSize = 0, preferred = 0, granularity = 0;
    if (ASIOGetBufferSize(&minSize, &maxSize, &preferred, &granularity) != ASE_OK) return fail("the ASIO driver gave no buffer size");
    if (wantedRate > 0.0 && ASIOCanSampleRate(wantedRate) == ASE_OK) ASIOSetSampleRate(wantedRate);
    ASIOSampleRate rate = 0.0;
    if (ASIOGetSampleRate(&rate) != ASE_OK || rate <= 0.0) return fail("the ASIO driver gave no sample rate");

    asio.buffers.assign(inputs, ASIOBufferInfo{});
    for (long c = 0; c < inputs; c++){
        asio.buffers[c].isInput = ASIOTrue;
        asio.buffers[c].channelNum = c;
    }
    asio.callbacks.bufferSwitch = bufferSwitch;
    asio.callbacks.sampleRateDidChange = sampleRateDidChange;
    asio.callbacks.asioMessage = asioMessage;
    asio.callbacks.bufferSwitchTimeInfo = bufferSwitchTimeInfo;
    // The driver's preferred size: what its control panel is set to, which the player may have tuned
    if (ASIOCreateBuffers(asio.buffers.data(), inputs, preferred, &asio.callbacks) != ASE_OK) return fail("the ASIO driver couldn't make its buffers");

    asio.formats.assign(inputs, SampleFormat::Int32);
    for (long c = 0; c < inputs; c++){
        ASIOChannelInfo channel{};
        channel.channel = c;
        channel.isInput = ASIOTrue;
        if (ASIOGetChannelInfo(&channel) != ASE_OK || !formatOf(channel.type, asio.formats[c])){
            ASIODisposeBuffers();
            return fail("the ASIO driver's sample format isn't supported");
        }
    }
    long inputLatency = 0, outputLatency = 0;
    if (ASIOGetLatencies(&inputLatency, &outputLatency) != ASE_OK) inputLatency = preferred;

    asio.channels = (int)inputs;
    asio.bufferFrames = preferred;
    asio.sampleRate = rate;
    asio.inputLatency = inputLatency;
    asio.frames.assign((size_t)preferred * inputs, 0.0f);
    if (ASIOStart() != ASE_OK){
        ASIODisposeBuffers();
        return fail("the ASIO driver didn't start playing");
    }
    return "";
}

static void closeDriver(){
    ASIOStop();
    ASIODisposeBuffers();
    ASIOExit(); // also unloads the driver
}

// The driver's thread: ASIO drivers are COM objects that expect a single-threaded apartment, which the main thread
// isn't (miniaudio made it multithreaded). Everything the driver is asked is asked from here, and its windows (the
// control panel) get their messages pumped here.
static void driverThread(std::string driver, double wantedRate, HWND window, std::promise<std::string> started){
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    MSG message;
    PeekMessage(&message, nullptr, WM_USER, WM_USER, PM_NOREMOVE); // makes this thread's message queue now
    std::string error = openDriver(driver, wantedRate, window);
    bool running = error.empty();
    started.set_value(error);
    if (running){
        while (GetMessage(&message, nullptr, 0, 0) > 0){
            if (message.message == STOP_MESSAGE) break;
            if (message.message == PANEL_MESSAGE){
                ASIOControlPanel();
                continue;
            }
            TranslateMessage(&message);
            DispatchMessage(&message);
        }
        closeDriver();
    }
    delete asioDrivers; // made by loadAsioDriver, on this thread: it goes on this thread too
    asioDrivers = nullptr;
    CoUninitialize();
}

std::vector<std::string> asioDriverNames(){
    std::vector<std::string> names;
    HKEY drivers;
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "SOFTWARE\\ASIO", 0, KEY_READ, &drivers) != ERROR_SUCCESS) return names;
    char name[256];
    for (DWORD i = 0;; i++){
        DWORD length = sizeof(name);
        if (RegEnumKeyExA(drivers, i, name, &length, nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS) break;
        names.push_back(name);
    }
    RegCloseKey(drivers);
    return names;
}

bool startAsioInput(const std::string& driver, double sampleRate, AsioSink sink, std::string& error){
    stopAsioInput();
    asio.sink = sink;
    asio.restartRequested = false;
    std::promise<std::string> started;
    std::future<std::string> result = started.get_future();
    // The game's window, for drivers that show their settings over it
    HWND window = GetForegroundWindow();
    asio.thread = std::thread(driverThread, driver, sampleRate, window, std::move(started));
    asio.threadId = GetThreadId(asio.thread.native_handle());
    error = result.get();
    if (!error.empty()){
        asio.thread.join();
        return false;
    }
    asio.active = true;
    return true;
}

void stopAsioInput(){
    if (!asio.thread.joinable()) return;
    if (asio.active) PostThreadMessage(asio.threadId, STOP_MESSAGE, 0, 0);
    asio.thread.join();
    asio.active = false;
}

bool asioInputActive(){ return asio.active; }
int asioInputChannels(){ return asio.active ? asio.channels : 0; }
double asioInputSampleRate(){ return asio.active ? asio.sampleRate : 0.0; }
int asioBufferFrames(){ return asio.active ? (int)asio.bufferFrames : 0; }
int asioInputLatencyFrames(){ return asio.active ? (int)asio.inputLatency : 0; }
bool asioRestartRequested(){ return asio.active && asio.restartRequested; }

void openAsioControlPanel(){
    if (asio.active) PostThreadMessage(asio.threadId, PANEL_MESSAGE, 0, 0);
}

#else

std::vector<std::string> asioDriverNames(){ return {}; }
bool startAsioInput(const std::string&, double, AsioSink, std::string& error){
    error = "ASIO is only on Windows";
    return false;
}
void stopAsioInput(){}
bool asioInputActive(){ return false; }
int asioInputChannels(){ return 0; }
double asioInputSampleRate(){ return 0.0; }
int asioBufferFrames(){ return 0; }
int asioInputLatencyFrames(){ return 0; }
bool asioRestartRequested(){ return false; }
void openAsioControlPanel(){}

#endif
