#pragma once

#include "imgui.h"
#include "raylib.h"
#include "ui/fretboardview.h"

// Play mode's notes (views/neckview) on a neck drawn with ImGui (ui/fretboardview): what the Instrument screen and the
// exercises draw notes with, so every neck in hardthz reads the same. A note is a rounded card in its string's color with
// its fret on it and its name under; the way between two notes is dots (a pluck), a tunnel (a slide) or a slur (a
// hammer-on, a pull-off); a bend is an arrow up from its note.

ImU32 imColor(Color color, float alpha = 1.0f);
Color blend(Color from, Color to, float t);

// A card's half width and half height on this neck
void cardSize(const FretboardLayout& board, float& halfW, float& halfH);
// A card's outline at `at`, grown by `grow` pixels all round (a ring, a place the note could be)
void cardOutline(ImDrawList* draw, ImVec2 at, float halfW, float halfH, float grow, ImU32 color, float width);
// A note's card on a string's fret: `grow` pixels added all round (a pop), `alpha` fading it, `newest` lit white
void drawNoteCard(ImDrawList* draw, const FretboardLayout& board, int string, int fret, int pitch, float grow, float alpha,
                  bool newest, float s);
// The way from the card at `a` to the one at `b`: dots, laid down by a light going along (`light` 0 to 1; negative:
// none)
void drawWay(ImDrawList* draw, const FretboardLayout& board, ImVec2 a, ImVec2 b, float light, float alpha, float s);
// A slide: a tunnel in the string's color, a light running through it
void drawTunnel(ImDrawList* draw, const FretboardLayout& board, ImVec2 a, ImVec2 b, Color color, float light, float alpha, float s);
// A hammer-on ("H") or a pull-off ("P"): a slur over the two cards, its letter at the top
void drawSlur(ImDrawList* draw, const FretboardLayout& board, ImVec2 a, ImVec2 b, const char* letter, Color color, float alpha, float s);
// A bend of `semitones` from the note on a string's fret: an arrow up from its card, and how far (1/4, 1/2, full...)
void drawBendArrow(ImDrawList* draw, const FretboardLayout& board, int string, int fret, float semitones, float s);
