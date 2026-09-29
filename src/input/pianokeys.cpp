#include "input/pianokeys.h"

#include "raylib.h"

#include <cstring>

// The keys that can play a note: letters, digits and the punctuation on the letter rows
struct NamedKey {
    const char* name;
    int code;
};
static const NamedKey KEYS[] = {
    {"A", KEY_A}, {"B", KEY_B}, {"C", KEY_C}, {"D", KEY_D}, {"E", KEY_E}, {"F", KEY_F}, {"G", KEY_G}, {"H", KEY_H},
    {"I", KEY_I}, {"J", KEY_J}, {"K", KEY_K}, {"L", KEY_L}, {"M", KEY_M}, {"N", KEY_N}, {"O", KEY_O}, {"P", KEY_P},
    {"Q", KEY_Q}, {"R", KEY_R}, {"S", KEY_S}, {"T", KEY_T}, {"U", KEY_U}, {"V", KEY_V}, {"W", KEY_W}, {"X", KEY_X},
    {"Y", KEY_Y}, {"Z", KEY_Z},
    {"0", KEY_ZERO}, {"1", KEY_ONE}, {"2", KEY_TWO}, {"3", KEY_THREE}, {"4", KEY_FOUR}, {"5", KEY_FIVE},
    {"6", KEY_SIX}, {"7", KEY_SEVEN}, {"8", KEY_EIGHT}, {"9", KEY_NINE},
    {",", KEY_COMMA}, {".", KEY_PERIOD}, {"/", KEY_SLASH}, {";", KEY_SEMICOLON}, {"'", KEY_APOSTROPHE},
    {"[", KEY_LEFT_BRACKET}, {"]", KEY_RIGHT_BRACKET}, {"-", KEY_MINUS}, {"=", KEY_EQUAL}, {"\\", KEY_BACKSLASH},
    {"`", KEY_GRAVE},
};

int pianoKeyCode(const std::string& name){
    for (const NamedKey& key : KEYS) if (name == key.name) return key.code;
    return 0;
}

std::string pianoKeyName(int code){
    for (const NamedKey& key : KEYS) if (code == key.code) return key.name;
    return "";
}

static struct {
    std::vector<int> codes;   // by slot, 0 for none
    std::vector<std::string> names;
    int base = 48;
    bool down[128] = {};
    std::vector<PlayedNote> played;
    bool active = false;
} piano;

void startPianoKeys(const std::vector<std::string>& keys, int base){
    piano.names = keys;
    piano.codes.clear();
    for (const std::string& name : keys) piano.codes.push_back(pianoKeyCode(name));
    piano.base = base;
    std::memset(piano.down, 0, sizeof(piano.down));
    piano.active = true;
}

void stopPianoKeys(){
    piano.active = false;
}

bool pianoKeysActive(){
    return piano.active;
}

const std::vector<PlayedNote>& updatePianoKeys(){
    piano.played.clear();
    if (!piano.active) return piano.played;
    // Up and Down move the whole keyboard an octave, for parts wider than it
    if (IsKeyPressed(KEY_UP) && piano.base + 12 + (int)piano.codes.size() <= 128) piano.base += 12;
    if (IsKeyPressed(KEY_DOWN) && piano.base >= 12) piano.base -= 12;
    std::memset(piano.down, 0, sizeof(piano.down));
    for (int slot = 0; slot < (int)piano.codes.size(); slot++){
        int code = piano.codes[slot], pitch = piano.base + slot;
        if (code == 0 || pitch < 0 || pitch > 127) continue;
        piano.down[pitch] = IsKeyDown(code);
        if (IsKeyPressed(code)) piano.played.push_back({pitch, 0.0f, 0.0});
    }
    return piano.played;
}

const bool* pianoKeysDown(){
    return piano.down;
}

std::string pianoKeyFor(int pitch){
    int slot = pitch - piano.base;
    if (!piano.active || slot < 0 || slot >= (int)piano.names.size() || piano.codes[slot] == 0) return "";
    return piano.names[slot];
}
