#include "core/menunotes.h"

#include "core/music.h"

const MenuNoteMap& menuNoteMap(bool bass){
    static const MenuNoteMap GUITAR = { 40, 45, 50, 55, 59, 64, { 41, 43, 47, 48, 52, 53, 57, 60 }, { 40, 45, 50, 55, 59, 64 } };
    static const MenuNoteMap BASS = { 28, 33, 38, 43, 47, 48, { 29, 31, 35, 36, 40, 41, 45 }, { 28, 33, 38, 43 } };
    return bass ? BASS : GUITAR;
}

MenuNoteAction menuNoteAction(const MenuNoteMap& map, int pitch, int& item){
    item = -1;
    if (pitch == map.back) return MenuNoteAction::Back;
    if (pitch == map.up) return MenuNoteAction::Up;
    if (pitch == map.down) return MenuNoteAction::Down;
    if (pitch == map.choose) return MenuNoteAction::Choose;
    if (pitch == map.left) return MenuNoteAction::Left;
    if (pitch == map.right) return MenuNoteAction::Right;
    for (int i = 0; i < (int)map.items.size(); i++){
        if (map.items[i] != pitch) continue;
        item = i;
        return MenuNoteAction::Item;
    }
    return MenuNoteAction::None;
}

std::string menuNotePlace(const MenuNoteMap& map, int pitch){
    // Where it's lowest on the neck; a string by its note, the low and the high E told apart
    int best = -1, fret = 0;
    for (int string = 0; string < (int)map.tuning.size(); string++){
        const int f = pitch - map.tuning[string];
        if (f >= 0 && f <= 24 && (best < 0 || f < fret)){
            best = string;
            fret = f;
        }
    }
    if (best < 0) return "";
    std::string name = pitchClassName(map.tuning[best]);
    if (map.tuning.size() == 6 && (best == 0 || best == 5)) name = (best == 0 ? "low " : "high ") + name;
    return name + (fret == 0 ? " open" : " " + std::to_string(fret));
}
