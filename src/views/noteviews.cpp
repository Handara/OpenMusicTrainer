#include "views/noteviews.h"

#include "views/neckview.h"
#include "views/pianohighway.h"
#include "views/staff.h"

#include <algorithm>

const float VIEW_GAP = 10.0f;

// Each view's share of the area when both are shown, and the most it ever needs: past that it would only get
// bigger, not clearer
enum class View { Staff, Neck };
struct ViewSize { float weight; float maxHeight; };
const ViewSize STAFF_SIZE = { 1.0f, 400.0f };
const ViewSize NECK_SIZE = { 1.0f, 420.0f };

float drawNoteViews(Rectangle area, const NoteViews& views, const std::vector<PlayNote>& notes, const Score& score,
                   const std::vector<int>& tuning, bool lowStringOnTop, TimeAxis axis){
    // The neck alone: it has no time axis, so it takes the whole area, and judgements go over its middle
    if (views.neck && !views.staff){
        drawNeckView(area, notes, tuning, lowStringOnTop, views.wholeNeck, views.label, axis);
        return area.x + area.width / 2;
    }

    // Top to bottom: the sheet music, then the neck
    struct Shown { View view; ViewSize size; float height; };
    std::vector<Shown> shown;
    if (views.staff) shown.push_back({View::Staff, STAFF_SIZE, 0.0f});
    if (views.neck) shown.push_back({View::Neck, NECK_SIZE, 0.0f});
    if (shown.empty()) return axis.hitLineX;

    float totalWeight = 0.0f;
    for (const Shown& view : shown) totalWeight += view.size.weight;
    float available = area.height - VIEW_GAP * (shown.size() - 1);
    float stackHeight = VIEW_GAP * (shown.size() - 1);
    for (Shown& view : shown){
        view.height = std::min(view.size.maxHeight, available * view.size.weight / totalWeight);
        stackHeight += view.height;
    }

    // The hit line stays clear of the sheet music's clef, key and time signature, and bar lines leave room for a
    // downbeat's accidental
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
            case View::Staff: drawStaff(viewArea, notes, score, axis); break;
            case View::Neck:  drawNeckView(viewArea, notes, tuning, lowStringOnTop, views.wholeNeck, views.label, axis); break;
        }
        y += shownView.height + VIEW_GAP;
    }
    return axis.hitLineX;
}

float drawKeysViews(Rectangle area, const NoteViews& views, const std::vector<PlayNote>& notes, const Score& score,
                    TimeAxis axis, const bool* keysDown, std::string (*keyLabel)(int pitch)){
    Rectangle piano = area;
    if (views.staff){
        float staffHeight = std::min(STAFF_SIZE.maxHeight, area.height * 0.4f);
        Rectangle staff = { area.x, area.y, area.width, staffHeight };
        axis.hitLineX = std::max(axis.hitLineX, area.x + staffLeadWidth(staffHeight, score));
        axis.barLineGap = std::max(axis.barLineGap, staffBarLineGap(staffHeight));
        drawStaff(staff, notes, score, axis);
        piano = { area.x, area.y + staffHeight + VIEW_GAP, area.width, area.height - staffHeight - VIEW_GAP };
    }
    drawPianoHighway(piano, notes, axis, keysDown, keyLabel);
    return area.x + area.width / 2;
}
