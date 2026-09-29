#pragma once

#include "core/chart.h"

#include <vector>

// Rhythm mode, taiko-style: any part of any song played as pure rhythm. Every moment the part plays something is
// one hit, and only its timing counts. Hits come in two kinds, like taiko's don and ka: the part's low notes are
// dons (a deep drum), its high notes kas (a bright rim), so a melody's shape still shows. A chord is a big hit.

enum class RhythmHitKind { Don, Ka };

struct RhythmHit {
    int tick;
    RhythmHitKind kind;
    bool big;          // a chord: several notes at once
};

// The part's hits, in time order. A moment's pitch is its highest note; at or above the part's middle pitch (the
// median of those moments) it's a ka, below it a don.
std::vector<RhythmHit> rhythmHits(const Chart& chart, int part);
