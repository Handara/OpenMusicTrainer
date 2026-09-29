#include "ui/hitfeedback.h"

#include "raylib.h"
#include "ui/theme.h"

#include <algorithm>
#include <cmath>
#include <cstring>

const size_t KEEP_RECENT = 32;          // hits shown on the timing bar
const double POPUP_S = 0.45;            // how long a judgement stays up
const double BAR_TICK_S = 4.0;          // how long a hit's tick stays on the timing bar, fading
const double PULSE_S = 0.12;
const double MILESTONE_S = 0.8;
const double BREAK_S = 1.1;
const int MILESTONE_EVERY = 50;
const int BREAK_WORTH_SHOWING = 10;     // a smaller combo breaking isn't news
const float BAR_HALF_WIDTH = 150.0f;    // the bar's half, at scale 1, covers the whole near window

static void remember(HitFeedback& feedback, HitFeedback::Judged judged){
    feedback.recent.push_back(judged);
    if (feedback.recent.size() > KEEP_RECENT) feedback.recent.erase(feedback.recent.begin());
}

void feedbackHit(HitFeedback& feedback, Judgement judgement, double errorSeconds, int combo){
    double now = GetTime();
    remember(feedback, { now, false, judgement == Judgement::Perfect, (float)(errorSeconds * 1000.0) });
    feedback.comboPulseAt = now;
    if (combo > 0 && combo % MILESTONE_EVERY == 0){
        feedback.milestone = combo;
        feedback.milestoneAt = now;
    }
}

void feedbackMiss(HitFeedback& feedback, int comboBefore){
    double now = GetTime();
    remember(feedback, { now, true, false, 0.0f });
    if (comboBefore >= BREAK_WORTH_SHOWING){
        feedback.broken = comboBefore;
        feedback.brokeAt = now;
    }
}

// Eases out: fast at first, settling at the end
static float easeOut(float t){
    t = std::clamp(t, 0.0f, 1.0f);
    return 1.0f - (1.0f - t) * (1.0f - t);
}

static void centeredText(ImDrawList* draw, ImFont* font, float size, ImVec2 center, ImU32 color, const char* text){
    ImVec2 measured = font ? font->CalcTextSizeA(size, FLT_MAX, 0.0f, text) : ImVec2(size * 0.6f * (float)strlen(text), size);
    draw->AddText(font, size, ImVec2(center.x - measured.x / 2, center.y - measured.y / 2), color, text);
}

void drawHitFeedback(const HitFeedback& feedback, ImDrawList* draw, int combo, const HitFeedbackLayout& layout){
    const UiFonts& fonts = uiFonts();
    const float s = layout.scale;
    const double now = GetTime();

    // The latest judgement, at the hit line: it pops in a little large, settles, rises and fades
    if (!feedback.recent.empty()){
        const HitFeedback::Judged& last = feedback.recent.back();
        float age = (float)(now - last.at);
        if (age < POPUP_S){
            float t = age / (float)POPUP_S, pop = 1.0f + 0.25f * (1.0f - easeOut(t * 3.0f));
            float alpha = t < 0.6f ? 1.0f : 1.0f - (t - 0.6f) / 0.4f;
            UiColor color = last.missed ? UiColor::Bad : (last.perfect ? UiColor::Good : UiColor::Accent);
            const char* word = last.missed ? "MISS" : (last.perfect ? "PERFECT" : "GOOD");
            ImVec2 at(layout.hitLineX, layout.judgementY - 10 * s * easeOut(t));
            centeredText(draw, fonts.heavy, 26 * s * pop, at, uiColor(color, alpha), word);
            if (!last.missed && !last.perfect){
                // Which way it was off, so the player knows how to correct
                const char* way = TextFormat("%s %.0f MS", last.errorMs > 0 ? "EARLY" : "LATE", std::fabs(last.errorMs));
                centeredText(draw, fonts.mono, 12 * s, ImVec2(at.x, at.y + 22 * s), uiColor(UiColor::Dim, alpha), way);
            }
        }
    }

    // The timing bar: early on the left, late on the right, like osu!'s. The perfect zone green, the rest of the
    // near window brass; each recent hit a tick where it landed, fading with age; a notch at their average.
    const float half = BAR_HALF_WIDTH * s, center = layout.barCenterX, y = layout.barY;
    const float perfectHalf = half * (float)(PERFECT_WINDOW_S / NEAR_WINDOW_S);
    draw->AddRectFilled(ImVec2(center - half, y - 2 * s), ImVec2(center + half, y + 2 * s), uiColor(UiColor::Accent, 0.35f), 2 * s);
    draw->AddRectFilled(ImVec2(center - perfectHalf, y - 2 * s), ImVec2(center + perfectHalf, y + 2 * s), uiColor(UiColor::Good, 0.6f), 2 * s);
    verticalLine(draw, center, y - 9 * s, y + 9 * s, 2.0f * s, uiColor(UiColor::Ink, 0.7f));
    float sum = 0.0f;
    int counted = 0;
    for (const HitFeedback::Judged& hit : feedback.recent){
        float age = (float)(now - hit.at);
        if (hit.missed || age > BAR_TICK_S) continue;
        float x = center - hit.errorMs / (float)(NEAR_WINDOW_S * 1000.0) * half; // an early hit (+) to the left
        float alpha = 1.0f - age / (float)BAR_TICK_S;
        verticalLine(draw, x, y - 7 * s, y + 7 * s, 2.0f * s, uiColor(hit.perfect ? UiColor::Good : UiColor::Accent, alpha));
        sum += hit.errorMs;
        counted++;
    }
    if (counted > 0){
        float x = center - sum / counted / (float)(NEAR_WINDOW_S * 1000.0) * half;
        draw->AddTriangleFilled(ImVec2(x - 5 * s, y - 14 * s), ImVec2(x + 5 * s, y - 14 * s), ImVec2(x, y - 8 * s), uiColor(UiColor::Ink));
    }
    draw->AddText(fonts.mono, 11 * s, ImVec2(center - half, y + 10 * s), uiColor(UiColor::Dim), "EARLY");
    float lateWidth = fonts.mono ? fonts.mono->CalcTextSizeA(11 * s, FLT_MAX, 0.0f, "LATE").x : 30 * s;
    draw->AddText(fonts.mono, 11 * s, ImVec2(center + half - lateWidth, y + 10 * s), uiColor(UiColor::Dim), "LATE");

    // The combo above the bar: it pulses with each hit, flashes brass on every 50th
    float comboY = y - 44 * s;
    if (combo > 1){
        float pulse = 1.0f + 0.18f * (1.0f - easeOut((float)((now - feedback.comboPulseAt) / PULSE_S)));
        float milestone = 1.0f - (float)((now - feedback.milestoneAt) / MILESTONE_S);
        ImU32 color = milestone > 0.0f ? uiColor(UiColor::Accent) : uiColor(UiColor::Ink);
        centeredText(draw, fonts.heavy, 38 * s * pulse, ImVec2(center, comboY), color, TextFormat("%d", combo));
        if (milestone > 0.0f){
            float grow = easeOut(1.0f - milestone);
            draw->AddCircle(ImVec2(center, comboY), (26 + 40 * grow) * s, uiColor(UiColor::Accent, milestone), 48, 3.0f * s);
        }
    }
    // A combo breaking: its number in red, shaking once and falling away
    float broke = (float)((now - feedback.brokeAt) / BREAK_S);
    if (broke < 1.0f){
        float shake = broke < 0.2f ? std::sin(broke * 80.0f) * 6.0f * s * (1.0f - broke * 5.0f) : 0.0f;
        centeredText(draw, fonts.bold, 18 * s, ImVec2(center + shake, comboY + 26 * s + 16 * s * easeOut(broke)),
                     uiColor(UiColor::Bad, 1.0f - broke), TextFormat("COMBO BROKEN  %d", feedback.broken));
    }
}
