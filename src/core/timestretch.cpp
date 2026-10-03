#include "core/timestretch.h"

#include <algorithm>
#include <cmath>

const float PIECE_S = 0.046f;  // a piece of sound: long enough for a bass's lowest note to have a few cycles in it
const float SEARCH_S = 0.012f; // a piece's place is nudged by up to this either way

void initTimeStretch(TimeStretch& stretch, int channels, int sampleRate, float speed){
    stretch = TimeStretch{};
    stretch.channels = std::max(1, channels);
    stretch.speed = std::clamp(speed, 0.02f, 2.0f); // it may be changed as it goes (a crawl: note by note)
    int window = 256;
    while (window < PIECE_S * sampleRate) window <<= 1;
    stretch.window = window;
    stretch.hop = window / 2;
    stretch.search = std::max(1, (int)(SEARCH_S * sampleRate));
    stretch.fade.resize(window);
    const float PI_F = 3.14159265358979f;
    for (int i = 0; i < window; i++) stretch.fade[i] = 0.5f - 0.5f * std::cos(2.0f * PI_F * i / window); // periodic: halves sum to 1
    stretch.mono.resize(window);
    resetTimeStretch(stretch, 0);
}

void resetTimeStretch(TimeStretch& stretch, long long songFrame){
    stretch.input.clear();
    stretch.inputStart = stretch.inputEnd = songFrame;
    stretch.next = (double)songFrame;
    stretch.previous = -1;
    stretch.tail.assign((size_t)stretch.hop * stretch.channels, 0.0f);
    stretch.ready.clear();
    stretch.ended = false;
}

// The furthest song frame the next piece may need: its own reach, nudged, and the stretch it's lined up with
static long long reach(const TimeStretch& stretch){
    long long start = (long long)std::llround(stretch.next) + stretch.search + stretch.window;
    if (stretch.previous >= 0) start = std::max(start, stretch.previous + stretch.hop + stretch.hop);
    return start;
}

int timeStretchWants(const TimeStretch& stretch){
    if (stretch.ended) return 0;
    return (int)std::max(0LL, reach(stretch) - stretch.inputEnd);
}

void feedTimeStretch(TimeStretch& stretch, const float* frames, int count){
    if (count <= 0) return;
    stretch.input.insert(stretch.input.end(), frames, frames + (size_t)count * stretch.channels);
    stretch.inputEnd += count;
}

void endTimeStretch(TimeStretch& stretch){
    stretch.ended = true;
}

// One song frame, mixed down, 0 outside what's fed
static float monoAt(const TimeStretch& stretch, long long frame){
    if (frame < stretch.inputStart || frame >= stretch.inputEnd) return 0.0f;
    const float* at = stretch.input.data() + (size_t)(frame - stretch.inputStart) * stretch.channels;
    float sum = 0.0f;
    for (int c = 0; c < stretch.channels; c++) sum += at[c];
    return sum;
}

// Makes one more half-piece of sound, if there's input enough for it
static bool makeOne(TimeStretch& stretch){
    if (!stretch.ended && stretch.inputEnd < reach(stretch)) return false;
    const long long nominal = (long long)std::llround(stretch.next);
    if (stretch.ended && nominal >= stretch.inputEnd) return false; // played out
    const int channels = stretch.channels, hop = stretch.hop, window = stretch.window;
    long long chosen = nominal;
    if (stretch.previous >= 0){
        // Where the last piece would naturally go on: the piece taken is the one, near its own place, whose start
        // looks most like that (the most correlated, every other frame compared, which is plenty)
        const long long natural = stretch.previous + hop;
        for (int i = 0; i < hop; i++) stretch.mono[i] = monoAt(stretch, natural + i);
        double best = -1e30;
        for (int d = -stretch.search; d <= stretch.search; d++){
            const long long start = nominal + d;
            if (start < stretch.inputStart) continue;
            double sum = 0.0;
            for (int i = 0; i < hop; i += 2) sum += stretch.mono[i] * monoAt(stretch, start + i);
            if (sum > best){ best = sum; chosen = start; }
        }
    }
    chosen = std::max(chosen, stretch.inputStart);
    // The piece, faded: its first half added to the last one's second half is sound made; its second half waits
    const size_t made = stretch.ready.size();
    stretch.ready.resize(made + (size_t)hop * channels);
    for (int i = 0; i < window; i++){
        const long long frame = chosen + i;
        const bool inside = frame >= stretch.inputStart && frame < stretch.inputEnd;
        const float* at = inside ? stretch.input.data() + (size_t)(frame - stretch.inputStart) * channels : nullptr;
        for (int c = 0; c < channels; c++){
            const float value = (at ? at[c] : 0.0f) * stretch.fade[i];
            if (i < hop) stretch.ready[made + (size_t)i * channels + c] = stretch.tail[(size_t)i * channels + c] + value;
            else stretch.tail[(size_t)(i - hop) * channels + c] = value;
        }
    }
    stretch.previous = chosen;
    stretch.next += hop * (double)stretch.speed;
    // What no piece will need again is let go
    const long long keep = std::min((long long)std::llround(stretch.next) - stretch.search, stretch.previous + hop);
    if (keep > stretch.inputStart){
        const long long drop = std::min(keep, stretch.inputEnd) - stretch.inputStart;
        stretch.input.erase(stretch.input.begin(), stretch.input.begin() + (size_t)drop * channels);
        stretch.inputStart += drop;
    }
    return true;
}

int takeTimeStretch(TimeStretch& stretch, float* out, int count){
    while ((int)(stretch.ready.size() / stretch.channels) < count && makeOne(stretch)){}
    const int got = std::min(count, (int)(stretch.ready.size() / stretch.channels));
    std::copy(stretch.ready.begin(), stretch.ready.begin() + (size_t)got * stretch.channels, out);
    stretch.ready.erase(stretch.ready.begin(), stretch.ready.begin() + (size_t)got * stretch.channels);
    return got;
}

double timeStretchLag(const TimeStretch& stretch){
    // A frame out is a blend of two pieces, taken half a piece and a piece from where the song would be at an even
    // pace: as heard, about half a piece's worth of difference, less at full speed (there's none at 1)
    return stretch.hop * (1.0 - stretch.speed);
}
