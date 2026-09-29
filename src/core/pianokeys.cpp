#include "core/pianokeys.h"

std::vector<std::string> defaultPianoKeys(){
    return { "Z", "S", "X", "D", "C", "V", "G", "B", "H", "N", "J", "M",          // C3 to B3: the bottom row
             "Q", "2", "W", "3", "E", "R", "5", "T", "6", "Y", "7", "U",          // C4 to B4: the top row
             "I", "9", "O", "0", "P" };                                          // and on to E5
}

int pianoBaseFor(int lowestPitch){
    return lowestPitch < 0 ? 0 : lowestPitch / 12 * 12;
}

void bindPianoKey(std::vector<std::string>& keys, int slot, const std::string& key){
    if (slot < 0 || slot >= (int)keys.size()) return;
    for (std::string& other : keys) if (other == key) other = "none";
    keys[slot] = key;
}
