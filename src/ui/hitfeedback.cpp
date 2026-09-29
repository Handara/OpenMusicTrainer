#include "ui/hitfeedback.h"

#include "raylib.h"
#include "ui/theme.h"

#include <algorithm>
#include <cmath>
#include <cstring>

const size_t KEEP_RECENT = 32;          // hits shown on the timing bar
const double POPUP_S = 0.45;            // how long a judgement stays up
const double PULSE_S = 0.12;
const double MILESTONE_S = 0.8;
const double BREAK_S = 1.1;
const int MILESTONE_EVERY = 50;
const int BREAK_WORTH_SHOWING = 10;     // a smaller combo breaking isn't news
const float BAR_HALF_WIDTH = 150.0f;    // the distribution's half, at scale 1, covers the whole near window
const float DISTRIBUTION_HEIGHT = 40.0f;
const float COMBO_GAP = 70.0f;          // the combo's middle, this far left of the distribution
const double LATEST_S = 0.6;            // the latest hit's mark fades over this long
const float BIN_MS = 5.0f;

static void remember(HitFeedback& feedback, HitFeedback::Judged judged){
    feedback.recent.push_back(judged);
    if (feedback.recent.size() > KEEP_RECENT) feedback.recent.erase(feedback.recent.begin());
}

void feedbackHit(HitFeedback& feedback, Judgement judgement, double errorSeconds, int combo, ImVec2 anchor){
    double now = GetTime();
    remember(feedback, { now, false, judgement == Judgement::Perfect, (float)(errorSeconds * 1000.0), anchor });
    feedback.comboPulseAt = now;
    if (combo > 0 && combo % MILESTONE_EVERY == 0){
        feedback.milestone = combo;
        feedback.milestoneAt = now;
    }
}

void feedbackMiss(HitFeedback& feedback, int comboBefore, ImVec2 anchor){
    double now = GetTime();
    remember(feedback, { now, true, false, 0.0f, anchor });
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

void drawHitFeedback(const HitFeedback& feedback, ImDrawList* draw, int combo, const std::vector<float>& errorsMs,
                     const HitFeedbackLayout& layout){
    const UiFonts& fonts = uiFonts();
    const float s = layout.scale;
    const double now = GetTime();

    // The latest judgement, at the hit line or over the note it was for: it pops in a little large, settles, rises
    // and fades
    if (!feedback.recent.empty()){
        const HitFeedback::Judged& last = feedback.recent.back();
        float age = (float)(now - last.at);
        if (age < POPUP_S){
            float t = age / (float)POPUP_S, pop = 1.0f + 0.25f * (1.0f - easeOut(t * 3.0f));
            float alpha = t < 0.6f ? 1.0f : 1.0f - (t - 0.6f) / 0.4f;
            UiColor color = last.missed ? UiColor::Bad : (last.perfect ? UiColor::Good : UiColor::Accent);
            const char* word = last.missed ? "MISS" : (last.perfect ? "PERFECT" : "GOOD");
            ImVec2 base = last.anchor.x >= 0.0f ? last.anchor : ImVec2(layout.hitLineX, layout.judgementY);
            ImVec2 at(base.x, base.y - 10 * s * easeOut(t));
            centeredText(draw, fonts.heavy, 26 * s * pop, at, uiColor(color, alpha), word);
            if (!last.missed && !last.perfect){
                // Which way it was off, so the player knows how to correct
                const char* way = TextFormat("%s %.0f MS", last.errorMs > 0 ? "EARLY" : "LATE", std::fabs(last.errorMs));
                centeredText(draw, fonts.mono, 12 * s, ImVec2(at.x, at.y + 22 * s), uiColor(UiColor::Dim, alpha), way);
            }
        }
    }

    // The run's timing so far, as a distribution under the notes (see drawTimingDistribution)
    ImVec2 area = hitDistributionArea(layout);
    const float half = BAR_HALF_WIDTH * s;
    double latestAt = -100.0;
    float latestMs = 0.0f;
    for (const HitFeedback::Judged& hit : feedback.recent) if (!hit.missed){ latestAt = hit.at; latestMs = hit.errorMs; }
    float latestAlpha = std::max(0.0f, 1.0f - (float)((now - latestAt) / LATEST_S));
    drawTimingDistribution(draw, errorsMs, area, ImVec2(2 * half, DISTRIBUTION_HEIGHT * s), s, DistributionLook{}, latestMs, latestAlpha);

    // The combo left of it: it pulses with each hit, flashes brass on every 50th
    const float center = area.x - COMBO_GAP * s, comboY = area.y + DISTRIBUTION_HEIGHT * s * 0.55f;
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

ImVec2 hitDistributionArea(const HitFeedbackLayout& layout){
    float s = layout.scale;
    return ImVec2(layout.barCenterX - BAR_HALF_WIDTH * s, layout.barY - DISTRIBUTION_HEIGHT * s);
}

ImVec2 hitDistributionSize(float scale){
    return ImVec2(2 * BAR_HALF_WIDTH * scale, DISTRIBUTION_HEIGHT * scale);
}

void drawTimingDistribution(ImDrawList* draw, const std::vector<float>& errorsMs, ImVec2 topLeft, ImVec2 size, float scale,
                            const DistributionLook& look, float latestMs, float latestAlpha){
    const UiFonts& fonts = uiFonts();
    const float s = scale, windowMs = (float)(NEAR_WINDOW_S * 1000.0), perfectMs = (float)(PERFECT_WINDOW_S * 1000.0);
    const int bins = (int)(2 * windowMs / BIN_MS);
    const float baseline = topLeft.y + size.y, center = topLeft.x + size.x / 2, half = size.x / 2;
    const float usable = size.y - 4 * s; // the tallest bar or peak reaches this high
    auto xAt = [&](float errorMs){ return center - errorMs / windowMs * half; }; // early (+) to the left

    // The axis, green where a hit is perfect, brass for the rest of the window
    const float perfectHalf = half * perfectMs / windowMs;
    draw->AddRectFilled(ImVec2(center - half, baseline - 1.5f * s), ImVec2(center + half, baseline + 1.5f * s), uiColor(UiColor::Accent, 0.35f));
    draw->AddRectFilled(ImVec2(center - perfectHalf, baseline - 1.5f * s), ImVec2(center + perfectHalf, baseline + 1.5f * s), uiColor(UiColor::Good, 0.6f));

    // The hits as thin bars, one per 5 ms bin, as tall as its share of the tallest
    if (look.bars > 0.0f){
        std::vector<int> counts(bins, 0);
        for (float error : errorsMs){
            int bin = (int)((windowMs - error) / BIN_MS);
            if (bin >= 0 && bin < bins) counts[bin]++;
        }
        int tallest = std::max(1, *std::max_element(counts.begin(), counts.end()));
        float binWidth = size.x / bins, barWidth = std::min(binWidth - 1.0f * s, 2.5f * s);
        for (int bin = 0; bin < bins; bin++){
            if (counts[bin] == 0) continue;
            float binCenterMs = windowMs - (bin + 0.5f) * BIN_MS;
            bool perfect = std::fabs(binCenterMs) <= perfectMs;
            float barHeight = std::max(2.0f * s, usable * counts[bin] / tallest);
            float x = topLeft.x + (bin + 0.5f) * binWidth;
            draw->AddRectFilled(ImVec2(x - barWidth / 2, baseline - barHeight), ImVec2(x + barWidth / 2, baseline),
                                uiColor(perfect ? UiColor::Good : UiColor::Accent, 0.85f * look.bars));
        }
    }

    // The same hits as a smooth curve: each one a small bell, their sum drawn (a kernel density estimate). The bells'
    // width follows how spread the hits are and how many there are (Silverman's rule), within 4 to 15 ms.
    if ((look.curve > 0.0f || look.fill > 0.0f) && !errorsMs.empty()){
        const int points = 160;
        float mean = 0.0f, spread = 0.0f;
        for (float error : errorsMs) mean += error;
        mean /= errorsMs.size();
        for (float error : errorsMs) spread += (error - mean) * (error - mean);
        spread = std::sqrt(spread / errorsMs.size());
        float width = std::clamp(1.06f * spread * std::pow((float)errorsMs.size(), -0.2f), 4.0f, 15.0f);
        std::vector<ImVec2> curve(points + 1);
        std::vector<float> density(points + 1, 0.0f);
        float peak = 0.0f;
        for (int k = 0; k <= points; k++){
            float ms = windowMs - 2 * windowMs * k / points; // left (early) to right (late)
            for (float error : errorsMs){
                float z = (ms - error) / width;
                density[k] += std::exp(-0.5f * z * z);
            }
            peak = std::max(peak, density[k]);
        }
        for (int k = 0; k <= points; k++) curve[k] = ImVec2(topLeft.x + size.x * k / points, baseline - usable * density[k] / std::max(peak, 1e-6f));

        // The area under it, its integral: green over the perfect window, brass on either side
        // (each region one shape, so no seams show between slices of it)
        if (look.fill > 0.0f){
            // The curve's height at any x, between its samples: the regions meet exactly on their borders
            auto curveY = [&](float x){
                float at = std::clamp((x - topLeft.x) / size.x * points, 0.0f, (float)points);
                int k = std::min((int)at, points - 1);
                return curve[k].y + (curve[k + 1].y - curve[k].y) * (at - k);
            };
            auto region = [&](float fromX, float toX, UiColor color){
                std::vector<ImVec2> shape = { ImVec2(fromX, baseline), ImVec2(fromX, curveY(fromX)) };
                for (const ImVec2& point : curve) if (point.x > fromX && point.x < toX) shape.push_back(point);
                shape.push_back(ImVec2(toX, curveY(toX)));
                shape.push_back(ImVec2(toX, baseline));
                if (shape.size() >= 3) draw->AddConcavePolyFilled(shape.data(), (int)shape.size(), uiColor(color, 0.35f * look.fill));
            };
            region(center - half, center - perfectHalf, UiColor::Accent);
            region(center - perfectHalf, center + perfectHalf, UiColor::Good);
            region(center + perfectHalf, center + half, UiColor::Accent);
        }
        // Traced from left to right: `curve` of the way along
        int drawnTo = (int)(look.curve * points);
        if (drawnTo > 0){
            std::vector<ImVec2> traced(curve.begin(), curve.begin() + drawnTo + 1);
            if (drawnTo < points){ // the pen's tip, partway to the next point
                float part = look.curve * points - drawnTo;
                ImVec2 a = curve[drawnTo], b = curve[drawnTo + 1];
                traced.push_back(ImVec2(a.x + (b.x - a.x) * part, a.y + (b.y - a.y) * part));
            }
            draw->AddPolyline(traced.data(), (int)traced.size(), uiColor(UiColor::Ink), ImDrawFlags_None, 2.0f * s);
        }
    }
    verticalLine(draw, center, topLeft.y, baseline + 6 * s, 1.5f * s, uiColor(UiColor::Ink, 0.5f));

    // The average, a notch under the axis; the latest hit, a mark fading out
    if (!errorsMs.empty()){
        float mean = 0.0f;
        for (float error : errorsMs) mean += error;
        mean /= errorsMs.size();
        float x = xAt(std::clamp(mean, -windowMs, windowMs));
        draw->AddTriangleFilled(ImVec2(x, baseline + 3 * s), ImVec2(x - 5 * s, baseline + 10 * s), ImVec2(x + 5 * s, baseline + 10 * s), uiColor(UiColor::Ink));
    }
    if (latestAlpha > 0.0f) verticalLine(draw, xAt(std::clamp(latestMs, -windowMs, windowMs)), topLeft.y - 4 * s, baseline, 2.0f * s, uiColor(UiColor::Ink, latestAlpha));

    // Under the axis: EARLY and LATE at its ends, or, once the area is filled in, each region's share of the hits
    float labelSize = 11 * s;
    auto label = [&](const char* text, float x, float anchor, ImU32 color){
        float textWidth = fonts.mono ? fonts.mono->CalcTextSizeA(labelSize, FLT_MAX, 0.0f, text).x : 30 * s;
        draw->AddText(fonts.mono, labelSize, ImVec2(x - textWidth * anchor, baseline + 12 * s), color, text);
    };
    if (look.fill > 0.0f && !errorsMs.empty()){
        int early = 0, perfect = 0, late = 0;
        for (float error : errorsMs) (error > perfectMs ? early : error < -perfectMs ? late : perfect)++;
        float total = (float)errorsMs.size();
        ImU32 dim = uiColor(UiColor::Dim, look.fill), good = uiColor(UiColor::Good, look.fill);
        label(TextFormat("EARLY %.0f%%", 100 * early / total), (center - half + center - perfectHalf) / 2, 0.5f, dim);
        label(TextFormat("PERFECT %.0f%%", 100 * perfect / total), center, 0.5f, good);
        label(TextFormat("LATE %.0f%%", 100 * late / total), (center + perfectHalf + center + half) / 2, 0.5f, dim);
    } else {
        label("EARLY", center - half, 0.0f, uiColor(UiColor::Dim));
        label("LATE", center + half, 1.0f, uiColor(UiColor::Dim));
    }
}
