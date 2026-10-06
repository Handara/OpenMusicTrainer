#include "doctest/doctest.h"

#include "core/menunotes.h"

#include <set>

TEST_CASE("menu notes: the open strings as arrows, a note for each item, none heard twice"){
    for (bool bass : { false, true }){
        const MenuNoteMap& map = menuNoteMap(bass);
        std::set<int> all = { map.back, map.up, map.down, map.choose, map.left, map.right };
        for (int note : map.items) all.insert(note);
        CHECK(all.size() == 6 + map.items.size()); // every note does one thing
        CHECK(map.items.size() >= 7);              // the main menu's items, each its own
        int item = 0;
        CHECK(menuNoteAction(map, map.back, item) == MenuNoteAction::Back);
        CHECK(menuNoteAction(map, map.choose, item) == MenuNoteAction::Choose);
        CHECK(menuNoteAction(map, map.items[2], item) == MenuNoteAction::Item);
        CHECK(item == 2);
        CHECK(menuNoteAction(map, 100, item) == MenuNoteAction::None);
        CHECK(item == -1);
    }
    const MenuNoteMap& guitar = menuNoteMap(false);
    CHECK(guitar.back == 40);  // the open low E
    CHECK(guitar.right == 64); // the open high E
    CHECK(menuNotePlace(guitar, 41) == "low E 1");
    CHECK(menuNotePlace(guitar, 48) == "A 3");
    CHECK(menuNotePlace(guitar, 55) == "G open");
    CHECK(menuNotePlace(menuNoteMap(true), 47) == "G 4");
}
