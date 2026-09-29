#include "input/midi.h"

#include "core/midi.h"

#include <atomic>
#include <chrono>
#include <cstring>
#include <mutex>
#include <thread>

#ifdef _WIN32
    #define WIN32_LEAN_AND_MEAN
    #define NOMINMAX
    #include <windows.h>
    #include <mmsystem.h>
#elif defined(__linux__)
    #include <fcntl.h>
    #include <poll.h>
    #include <unistd.h>
    #include <filesystem>
    #include <fstream>
    #include <sstream>
#endif

using Clock = std::chrono::steady_clock;

// What the reading thread hands over: each event and when it arrived
struct StampedEvent {
    MidiEvent event;
    Clock::time_point at;
};

static struct {
    std::mutex lock;                    // guards `arrived`, filled by the reading thread
    std::vector<StampedEvent> arrived;
    std::vector<StampedEvent> taking;   // swapped out under the lock, then read without it
    std::vector<PlayedNote> played;
    bool keysDown[128] = {};
    MidiParser parser;                  // only the reading thread touches it
    std::string deviceName;
    bool active = false;
#ifdef _WIN32
    HMIDIIN handle = nullptr;
#elif defined(__linux__)
    int fd = -1;
    std::thread reader;
    std::atomic<bool> stop{false};
#endif
} midi;

static void deliver(const uint8_t* bytes, int count){
    std::vector<MidiEvent> events;
    feedMidi(midi.parser, bytes, count, events);
    if (events.empty()) return;
    Clock::time_point now = Clock::now();
    std::lock_guard<std::mutex> guard(midi.lock);
    for (const MidiEvent& event : events) midi.arrived.push_back({event, now});
}

#ifdef _WIN32

// winmm calls this on a thread of its own as each message arrives: bytes packed into dwParam1
static void CALLBACK midiCallback(HMIDIIN, UINT message, DWORD_PTR, DWORD_PTR param1, DWORD_PTR){
    if (message != MIM_DATA) return;
    uint8_t bytes[3] = { (uint8_t)(param1 & 0xFF), (uint8_t)((param1 >> 8) & 0xFF), (uint8_t)((param1 >> 16) & 0xFF) };
    uint8_t kind = bytes[0] & 0xF0;
    int count = (kind == 0xC0 || kind == 0xD0) ? 2 : (bytes[0] >= 0xF8 ? 1 : 3);
    deliver(bytes, count);
}

std::vector<std::string> midiDeviceNames(){
    std::vector<std::string> names;
    UINT count = midiInGetNumDevs();
    for (UINT i = 0; i < count; i++){
        MIDIINCAPSA caps;
        if (midiInGetDevCapsA(i, &caps, sizeof(caps)) == MMSYSERR_NOERROR) names.push_back(caps.szPname);
    }
    return names;
}

static bool openDevice(const std::string& name, std::string& error){
    std::vector<std::string> names = midiDeviceNames();
    UINT id = 0;
    bool found = false;
    for (UINT i = 0; i < names.size() && !found; i++) if (name.empty() || names[i] == name){ id = i; found = true; }
    if (!found){
        error = names.empty() ? "no MIDI device is connected" : "the MIDI device '" + name + "' isn't connected";
        return false;
    }
    if (midiInOpen(&midi.handle, id, (DWORD_PTR)midiCallback, 0, CALLBACK_FUNCTION) != MMSYSERR_NOERROR){
        error = "could not open the MIDI device '" + names[id] + "'";
        return false;
    }
    midiInStart(midi.handle);
    midi.deviceName = names[id];
    return true;
}

static void closeDevice(){
    if (!midi.handle) return;
    midiInStop(midi.handle);
    midiInReset(midi.handle);
    midiInClose(midi.handle);
    midi.handle = nullptr;
}

#elif defined(__linux__)

// The kernel's raw MIDI devices: /dev/snd/midiC<card>D<device>, named after their sound card
struct LinuxDevice {
    std::string path;
    std::string name;
};

static std::vector<LinuxDevice> linuxDevices(){
    // Card names, from /proc/asound/cards: " 1 [Keystation     ]: USB-Audio - Keystation 49 MK3"
    std::vector<std::string> cardNames(32);
    std::ifstream cards("/proc/asound/cards");
    std::string line;
    while (std::getline(cards, line)){
        std::istringstream ss(line);
        int card;
        if (!(ss >> card) || card < 0 || card >= 32) continue;
        size_t dash = line.find(" - ");
        if (dash != std::string::npos) cardNames[card] = line.substr(dash + 3);
    }
    std::vector<LinuxDevice> devices;
    std::error_code ec;
    for (const std::filesystem::directory_entry& file : std::filesystem::directory_iterator("/dev/snd", ec)){
        std::string file_name = file.path().filename().string();
        int card = 0, device = 0;
        if (std::sscanf(file_name.c_str(), "midiC%dD%d", &card, &device) != 2) continue;
        std::string name = card < 32 && !cardNames[card].empty() ? cardNames[card] : "MIDI device " + std::to_string(card);
        if (device > 0) name += " (" + std::to_string(device + 1) + ")";
        devices.push_back({file.path().string(), name});
    }
    return devices;
}

std::vector<std::string> midiDeviceNames(){
    std::vector<std::string> names;
    for (const LinuxDevice& device : linuxDevices()) names.push_back(device.name);
    return names;
}

// The reading thread: waits for bytes (a twentieth of a second at most, to notice when it's told to stop)
static void readDevice(){
    uint8_t buffer[256];
    while (!midi.stop){
        pollfd waiting = { midi.fd, POLLIN, 0 };
        if (poll(&waiting, 1, 50) <= 0) continue;
        ssize_t got = read(midi.fd, buffer, sizeof(buffer));
        if (got > 0) deliver(buffer, (int)got);
        else if (got == 0 || (got < 0 && errno != EAGAIN)) break; // unplugged
    }
}

static bool openDevice(const std::string& name, std::string& error){
    std::vector<LinuxDevice> devices = linuxDevices();
    const LinuxDevice* chosen = nullptr;
    for (const LinuxDevice& device : devices) if (!chosen && (name.empty() || device.name == name)) chosen = &device;
    if (!chosen){
        error = devices.empty() ? "no MIDI device is connected" : "the MIDI device '" + name + "' isn't connected";
        return false;
    }
    midi.fd = open(chosen->path.c_str(), O_RDONLY | O_NONBLOCK);
    if (midi.fd < 0){
        error = "could not open " + chosen->path + " (is your user in the 'audio' group?)";
        return false;
    }
    midi.deviceName = chosen->name;
    midi.stop = false;
    midi.reader = std::thread(readDevice);
    return true;
}

static void closeDevice(){
    midi.stop = true;
    if (midi.reader.joinable()) midi.reader.join();
    if (midi.fd >= 0) close(midi.fd);
    midi.fd = -1;
}

#else

std::vector<std::string> midiDeviceNames(){ return {}; }
static bool openDevice(const std::string&, std::string& error){
    error = "MIDI isn't supported on this system yet";
    return false;
}
static void closeDevice(){}

#endif

bool startMidiInput(const std::string& device, std::string& error){
    stopMidiInput();
    midi.parser = MidiParser{};
    std::memset(midi.keysDown, 0, sizeof(midi.keysDown));
    {
        std::lock_guard<std::mutex> guard(midi.lock);
        midi.arrived.clear();
    }
    if (!openDevice(device, error)) return false;
    midi.active = true;
    return true;
}

void stopMidiInput(){
    if (!midi.active) return;
    closeDevice();
    midi.active = false;
    midi.deviceName.clear();
}

bool midiInputActive(){
    return midi.active;
}

const char* midiDeviceName(){
    return midi.deviceName.c_str();
}

const std::vector<PlayedNote>& updateMidiInput(){
    midi.played.clear();
    if (!midi.active) return midi.played;
    {
        std::lock_guard<std::mutex> guard(midi.lock);
        midi.taking.swap(midi.arrived);
    }
    Clock::time_point now = Clock::now();
    for (const StampedEvent& stamped : midi.taking){
        const MidiEvent& event = stamped.event;
        if (event.kind == MidiEvent::Kind::ControlChange || event.number < 0 || event.number > 127) continue;
        bool on = event.kind == MidiEvent::Kind::NoteOn;
        midi.keysDown[event.number] = on;
        if (on) midi.played.push_back({event.number, 0.0f, std::chrono::duration<double>(now - stamped.at).count()});
    }
    midi.taking.clear();
    return midi.played;
}

const bool* midiKeysDown(){
    return midi.keysDown;
}
