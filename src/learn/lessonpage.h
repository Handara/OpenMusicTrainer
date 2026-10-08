#pragma once

#include "core/lessondoc.h"
#include "imgui.h"
#include "raylib.h"

#include <map>
#include <string>
#include <vector>

// A lesson's page, drawn the way the student sees it: its title, then its sections, each laid out in its columns, each
// column's blocks one under the other. The lesson player shows it, and the lesson maker builds on the very same
// drawing, so what's made is what's seen.

// What a page needs loaded: its pictures (kept while it's shown), and the one sound or video playing (through the
// song stream, so one at a time)
struct PageMedia {
    std::map<std::string, Texture2D> textures; // by path
    std::string audioPath;
    std::string videoPath;
    std::string error;
};
void releasePageMedia(PageMedia& media); // when the page changes, and when leaving

// What the page shows of its scored blocks (drills, songs), in reading order (core/lessondoc scoredBlocks): which are
// passed, which is chosen with the keys, and what each runs (for its name and goal; nullptr: not known)
struct PageState {
    std::vector<bool> passed;
    int chosen = -1;
    std::vector<const ExerciseEntry*> exercises;
};

// What happened on the page this frame
struct PageEvents {
    int started = -1; // a scored block clicked: its number on the page
    std::vector<ImVec2> scoredSpans; // where each scored block was drawn: its top and bottom, on the screen
};

// Draws page `page` from `at`, `width` wide (the window's draw list); returns how tall it is
float drawLessonPage(const LessonDoc& doc, int page, const std::string& folder, PageMedia& media, const PageState& state,
                     PageEvents& events, ImVec2 at, float width, float scale);
