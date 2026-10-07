#include "input/keysinput.h"

#include "core/pianokeys.h"
#include "input/midi.h"
#include "input/pianokeys.h"

static bool keysMidi = false;
static const std::vector<PlayedNote> NO_NOTES;

void startKeysInput(const Settings& settings, int lowestPitch){
    stopKeysInput();
    std::string error;
    keysMidi = startMidiInput(settings.midiDevice, error);
    if (!keysMidi) startPianoKeys(settings.pianoKeys, pianoBaseFor(lowestPitch));
}

void stopKeysInput(){
    stopMidiInput();
    stopPianoKeys();
    keysMidi = false;
}

const std::vector<PlayedNote>& updateKeysInput(){
    if (keysMidi && midiInputActive()) return updateMidiInput();
    if (pianoKeysActive()) return updatePianoKeys();
    return NO_NOTES;
}

const bool* keysInputDown(){
    if (keysMidi && midiInputActive()) return midiKeysDown();
    if (pianoKeysActive()) return pianoKeysDown();
    return nullptr;
}

bool keysInputIsMidi(){
    return keysMidi && midiInputActive();
}

std::string keysInputLabel(int pitch){
    return keysInputIsMidi() || !pianoKeysActive() ? std::string() : pianoKeyFor(pitch);
}
