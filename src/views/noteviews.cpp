#include "views/noteviews.h"

#include "views/highway.h"
#include "views/staff.h"
#include "views/tab.h"

#include <algorithm>

const float VIEW_GAP = 10.0f;

// Each view's share of the area when several are shown, and the most it ever needs: past that it would only
// get bigger, not clearer (the highway's lanes stop spreading at 70 px, the staff is plenty readable at this size)
enum class View { Staff, Tab, Highway };
struct ViewSize { float weight; float maxHeight; };
const ViewSize STAFF_SIZE = { 1.0f, 400.0f };
const ViewSize TAB_SIZE = { 0.7f, 200.0f };
const ViewSize HIGHWAY_SIZE = { 1.0f, 460.0f };

void drawNoteViews(Rectangle area, const NoteViews& views, const std::vector<PlayNote>& notes, const Score& score,
                   const std::vector<int>& tuning, bool lowStringOnTop, TimeAxis axis){
    // Top to bottom: sheet music over tab, like a printed guitar score, then the highway
    struct Shown { View view; ViewSize size; float height; };
    std::vector<Shown> shown;
    if (views.staff) shown.push_back({View::Staff, STAFF_SIZE, 0.0f});
    if (views.tab) shown.push_back({View::Tab, TAB_SIZE, 0.0f});
    if (views.highway) shown.push_back({View::Highway, HIGHWAY_SIZE, 0.0f});
    if (shown.empty()) return;

    float totalWeight = 0.0f;
    for (const Shown& view : shown) totalWeight += view.size.weight;
    float available = area.height - VIEW_GAP * (shown.size() - 1);
    float stackHeight = VIEW_GAP * (shown.size() - 1);
    for (Shown& view : shown){
        view.height = std::min(view.size.maxHeight, available * view.size.weight / totalWeight);
        stackHeight += view.height;
    }

    // The hit line stays clear of the sheet music's clef, key and time signature, and bar lines leave room for a
    // downbeat's accidental: both set for every view at once, so they stay lined up
    for (const Shown& view : shown){
        if (view.view != View::Staff) continue;
        axis.hitLineX = std::max(axis.hitLineX, area.x + staffLeadWidth(view.height, score));
        axis.barLineGap = std::max(axis.barLineGap, staffBarLineGap(view.height));
    }

    // Centered in the area: space a capped view didn't take is shared above and below
    float y = area.y + (area.height - stackHeight) / 2;
    for (const Shown& shownView : shown){
        Rectangle viewArea = { area.x, y, area.width, shownView.height };
        switch (shownView.view){
            case View::Staff:   drawStaff(viewArea, notes, score, axis); break;
            case View::Tab:     drawTab(viewArea, notes, score, (int)tuning.size(), axis); break;
            case View::Highway: drawHighway(viewArea, notes, tuning, lowStringOnTop, axis); break;
        }
        y += shownView.height + VIEW_GAP;
    }
}
