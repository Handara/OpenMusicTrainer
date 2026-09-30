#include "views/pianohighway.h"

#include "ui/theme.h"
#include "views/smooth.h"
#include "views/viewfont.h"

#include <algorithm>

const float MAX_WHITE_KEY_WIDTH = 46.0f;
const float KEYBOARD_SHARE = 0.2f;       // of the view's height, for the keys themselves
const float BLACK_KEY_WIDTH = 0.6f;      // of a white key
const float BLACK_KEY_LENGTH = 0.62f;    // of a white key's length
const float NOTE_WIDTH = 0.8f;           // a falling note, of its key's width

static bool isBlack(int pitch){
    int pitchClass = pitch % 12;
    return pitchClass == 1 || pitchClass == 3 || pitchClass == 6 || pitchClass == 8 || pitchClass == 10;
}

// White keys from C (0) up: which one a pitch is on, or which it sits between (a black key: its left neighbour)
static int whiteIndex(int pitch){
    static const int WHITE_BEFORE[12] = { 0, 0, 1, 1, 2, 3, 3, 4, 4, 5, 5, 6 };
    return (pitch / 12) * 7 + WHITE_BEFORE[pitch % 12];
}

void drawPianoHighway(Rectangle area, const std::vector<PlayNote>& notes, const TimeAxis& axis, const bool* keysDown,
                      std::string (*keyLabel)(int pitch)){
    // The range: the part's notes, widened to whole octaves, two at least
    int lowest = 60, highest = 71;
    if (!notes.empty()){
        lowest = highest = notes[0].pitch;
        for (const PlayNote& note : notes){ lowest = std::min(lowest, note.pitch); highest = std::max(highest, note.pitch); }
    }
    lowest = lowest / 12 * 12;
    highest = highest / 12 * 12 + 11;
    if (highest - lowest < 23) highest = lowest + 23;
    int firstWhite = whiteIndex(lowest), whiteKeys = whiteIndex(highest) - firstWhite + 1;
    float keyWidth = std::min(MAX_WHITE_KEY_WIDTH, area.width / whiteKeys);
    float left = area.x + (area.width - keyWidth * whiteKeys) / 2;
    float keyboardTop = area.y + area.height * (1.0f - KEYBOARD_SHARE), keyboardBottom = area.y + area.height;
    float keyLength = keyboardBottom - keyboardTop;
    // Where a pitch's key is, across: its left edge and width
    auto keyX = [&](int pitch){
        if (!isBlack(pitch)) return left + (whiteIndex(pitch) - firstWhite) * keyWidth;
        return left + (whiteIndex(pitch) - firstWhite + 1) * keyWidth - keyWidth * BLACK_KEY_WIDTH / 2;
    };
    auto keyW = [&](int pitch){ return isBlack(pitch) ? keyWidth * BLACK_KEY_WIDTH : keyWidth; };
    auto yAt = [&](float time){ return keyboardTop - (time - axis.songTime) * axis.noteSpeed; };

    const Color card = themeColor(UiColor::Card), line = themeColor(UiColor::StaffLine), accent = themeColor(UiColor::Accent);
    DrawRectangleRec(area, card);
    // A faint line down from every C, and from every F, so the eye finds its place above the keys
    for (int pitch = lowest; pitch <= highest; pitch++){
        if (pitch % 12 == 0 || pitch % 12 == 5) DrawRectangleRec({keyX(pitch), area.y, 1.0f, keyboardTop - area.y}, line);
    }

    // The notes, falling: from where they start to where they end, the black keys' a darker brass
    BeginScissorMode((int)area.x, (int)area.y, (int)area.width, (int)(keyboardTop - area.y));
    float earliest = axis.songTime - 1.0f, latest = axis.songTime + (keyboardTop - area.y) / axis.noteSpeed;
    auto it = std::lower_bound(notes.begin(), notes.end(), earliest - 10.0f, [](const PlayNote& note, float time){ return note.time < time; });
    for (; it != notes.end() && it->time <= latest; ++it){
        const PlayNote& note = *it;
        if (note.hit && note.hitFlash <= 0.0f && note.time + note.length < axis.songTime) continue; // played, and over
        float bottom = yAt(note.time), top = yAt(note.time + std::max(note.length, 0.08f));
        float width = keyW(note.pitch) * NOTE_WIDTH, x = keyX(note.pitch) + (keyW(note.pitch) - width) / 2;
        Color color = isBlack(note.pitch) ? ColorBrightness(accent, -0.3f) : accent;
        if (note.hit) color = themeColor(note.wasPerfect ? UiColor::Good : UiColor::Accent);
        float alpha = note.judged && !note.hit ? 0.3f : 1.0f;             // a missed note: its ghost
        if (note.hit) bottom = std::min(bottom, keyboardTop);             // a held note is eaten by its key as it plays
        if (bottom <= top) continue;
        smoothRoundedRect({x, top, width, bottom - top}, 0.35f * std::min(width, bottom - top) / 2, Fade(color, alpha));
    }
    EndScissorMode();

    // The keyboard: white keys, then black keys over them; pressed keys light up, a hit bursts on its key
    DrawRectangleRec({left, keyboardTop - 2.0f, keyWidth * whiteKeys, 2.0f}, accent); // the hit line: the keys' edge
    auto keyColor = [&](int pitch, Color normal){
        if (keysDown && keysDown[pitch]) return ColorAlphaBlend(normal, Fade(accent, 0.55f), WHITE);
        return normal;
    };
    for (int pitch = lowest; pitch <= highest; pitch++){
        if (isBlack(pitch)) continue;
        Rectangle key = {keyX(pitch) + 0.5f, keyboardTop, keyWidth - 1.0f, keyLength};
        DrawRectangleRec(key, keyColor(pitch, WHITE));
        DrawRectangleLinesEx(key, 1.0f, line);
        if (pitch % 12 == 0){
            drawViewText(TextFormat("C%d", pitch / 12 - 1), key.x + key.width / 2, keyboardBottom - keyWidth * 0.45f,
                         std::max(9.0f, keyWidth * 0.36f), themeColor(UiColor::Dim));
        }
    }
    for (int pitch = lowest; pitch <= highest; pitch++){
        if (!isBlack(pitch)) continue;
        DrawRectangleRec({keyX(pitch), keyboardTop, keyW(pitch), keyLength * BLACK_KEY_LENGTH}, keyColor(pitch, {30, 30, 34, 255}));
    }
    // The computer key that plays each note, on its piano key: white keys near their bottom, black keys on them
    if (keyLabel){
        float size = std::max(9.0f, keyWidth * 0.34f);
        for (int pitch = lowest; pitch <= highest; pitch++){
            std::string label = keyLabel(pitch);
            if (label.empty()) continue;
            bool black = isBlack(pitch);
            float y = black ? keyboardTop + keyLength * BLACK_KEY_LENGTH - size : keyboardBottom - keyWidth * 0.9f;
            drawViewText(label.c_str(), keyX(pitch) + keyW(pitch) / 2, y, size, black ? WHITE : themeColor(UiColor::Ink));
        }
    }
    for (const PlayNote& note : notes){
        if (!note.hit || note.hitFlash <= 0.0f) continue;
        float t = note.hitFlash / HIT_FLASH_DURATION; // 1 at the hit, fading
        Color lit = themeColor(note.wasPerfect ? UiColor::Good : UiColor::Accent);
        float width = keyW(note.pitch), grow = (1.0f - t) * keyWidth * 0.6f;
        Rectangle burst = {keyX(note.pitch) - grow, keyboardTop - 6.0f - grow, width + 2 * grow, 6.0f + 2 * grow};
        smoothRoundedRect(burst, 0.5f * std::min(burst.width, burst.height) / 2, Fade(lit, t));
    }
}
