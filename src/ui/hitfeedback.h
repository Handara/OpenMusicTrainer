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
    float barCenterX;   // the timing distribution's middle, with the combo left of it...
    float barY;         // ...and its baseline
    float scale;        // 1 at a 720-pixel-tall window
};
// errorsMs: every hit of the run so far (+ early), for the distribution
void drawHitFeedback(const HitFeedback& feedback, ImDrawList* draw, int combo, const std::vector<float>& errorsMs,
                     const HitFeedbackLayout& layout);

// Where the distribution goes in that layout, and its size: for anything that wants to take it from there (the
// results screen, which grows it out of the play screen)
ImVec2 hitDistributionArea(const HitFeedbackLayout& layout);
ImVec2 hitDistributionSize(float scale);

// How the distribution shows, each from 0 (not at all) to 1: the bars; the curve, traced that far from the left;
// the area under it filled in, with each region's share of the hits
struct DistributionLook {
    float bars = 1.0f;
    float curve = 0.0f;
    float fill = 0.0f;
};

// The run's timing as a distribution across the near window, early on the left, green where it's perfect and brass
// for the rest, a notch at the average: as thin bars (5 ms bins) or as a smooth curve and the area under it; the
// latest hit marked (fading with latestAlpha, 0 for none)
void drawTimingDistribution(ImDrawList* draw, const std::vector<float>& errorsMs, ImVec2 topLeft, ImVec2 size, float scale,
                            const DistributionLook& look = {}, float latestMs = 0.0f, float latestAlpha = 0.0f);
