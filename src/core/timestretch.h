#pragma once

#include <vector>

// A song played slower (or faster) without its pitch changing, for practising: at 70% a song is still in the key the
// instrument is tuned to. WSOLA (waveform-similarity overlap-add): the sound is cut into overlapping pieces, each
// faded in and out, and laid back down with the same overlap; slower, each piece is taken from less far along the
// song than the last, so the song lasts longer while every piece keeps its own pitch. Where each piece is taken is
// nudged a little, to where its wave lines up best with the end of the piece before, so they join without a beat.
// Streaming: the song's frames are fed in order, the stretched sound taken out as it's made. Pure math.

struct TimeStretch {
    int channels = 1;
    float speed = 1.0f;          // song frames per frame out: 0.5 plays at half speed
    int window = 0;              // a piece's length, in frames
    int hop = 0;                 // how far apart pieces are laid down: half a piece
    int search = 0;              // how far either way a piece's place is nudged to line up
    std::vector<float> fade;     // the piece's fade in and out (a Hann window): two halves add up to one
    std::vector<float> input;    // the song's frames fed and not yet left behind (interleaved), from `inputStart`
    long long inputStart = 0;    // the song frame input[0] is
    long long inputEnd = 0;      // and one past the last fed
    double next = 0.0;           // where the next piece is taken from, before its nudge (a song frame)
    long long previous = -1;     // where the last piece was taken from, -1 before the first
    std::vector<float> tail;     // the last piece's second half, faded, waiting for the next piece's first half
    std::vector<float> ready;    // frames made and not yet taken (interleaved)
    std::vector<float> mono;     // scratch: the lining up is judged on the channels mixed
    bool ended = false;          // no more frames will be fed: what's left is played out
};

void initTimeStretch(TimeStretch& stretch, int channels, int sampleRate, float speed);
// Starting again at a song frame (after a seek): what was fed and made is dropped
void resetTimeStretch(TimeStretch& stretch, long long songFrame);
// How many more song frames it needs before it can make more sound (0: it has enough)
int timeStretchWants(const TimeStretch& stretch);
// The song's next frames, in order (interleaved); `count` 0 with nothing more to come says the song is over
void feedTimeStretch(TimeStretch& stretch, const float* frames, int count);
void endTimeStretch(TimeStretch& stretch);
// Takes up to `count` frames of the stretched sound; returns how many there were
int takeTimeStretch(TimeStretch& stretch, float* out, int count);
// How far behind the song frames fed the sound coming out is, on average: frame t out (since the reset at S) is the
// song around S + t * speed + this
double timeStretchLag(const TimeStretch& stretch);
