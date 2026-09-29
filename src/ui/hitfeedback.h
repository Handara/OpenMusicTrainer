#pragma once

#include "core/judge.h"
#include "imgui.h"

#include <vector>

// What a player sees when they hit or miss, the feel of a rhythm game: the judgement popping up at the hit line
// (PERFECT, GOOD with EARLY or LATE, MISS), a timing bar of the latest hits (osu!'s hit error meter: where each
// landed against the beat), and the combo, pulsing with each hit, flashing on every 50th and breaking visibly.
// The screen tells it what happened; it keeps the moments and draws them.

struct HitFeedback {
    struct Judged {
        double at;          // GetTime() when it happened
        bool missed;
        bool perfect;
        float errorMs;      // + early, - late
    };
    std::vector<Judged> recent;     // the latest, newest last: the popup and the timing bar
    double comboPulseAt = -100.0;   // the last hit, for the combo's pulse
    double milestoneAt = -100.0;    // the last 50th, 100th... hit
    int milestone = 0;
    double brokeAt = -100.0;        // the last combo break worth showing (10 or more)
    int broken = 0;
};

// A hit: the judgement and how far off it was (the judge's error: note time - input time, in seconds), and the
// combo after it
void feedbackHit(HitFeedback& feedback, Judgement judgement, double errorSeconds, int combo);
// A miss, with the combo it ended
void feedbackMiss(HitFeedback& feedback, int comboBefore);

struct HitFeedbackLayout {
    float hitLineX;     // the judgement pops up centered on it...
    float judgementY;   // ...at this height
    float barCenterX;   // the timing bar, and the combo just above it
    float barY;
    float scale;        // 1 at a 720-pixel-tall window
};
void drawHitFeedback(const HitFeedback& feedback, ImDrawList* draw, int combo, const HitFeedbackLayout& layout);
