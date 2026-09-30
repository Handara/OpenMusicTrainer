#include "doctest/doctest.h"

#include "core/judge.h"

static std::vector<PlayNote> notesAt(std::initializer_list<PlayNote> list){
    return std::vector<PlayNote>(list);
}

TEST_CASE("keyboard input matches notes by string, within the windows"){
    std::vector<PlayNote> notes = notesAt({{1.0f, 0, 0, 40}, {2.0f, 1, 0, 45}});

    PlayerInput press{1.02, 0};                          // string 0, 20 ms late
    JudgeResult r = judgeInput(notes, press);
    CHECK(r.judgement == Judgement::Perfect);
    CHECK(r.error == doctest::Approx(-0.02));
    CHECK(notes[0].hit);
    CHECK(notes[0].hitFlash > 0.0f);

    CHECK(judgeInput(notes, {2.07, 1}).judgement == Judgement::Near); // 70 ms late
    CHECK(judgeInput(notes, {2.0, 1}).judgement == Judgement::Ignored); // already hit
}

TEST_CASE("wrong string, stray presses and too early are ignored"){
    std::vector<PlayNote> notes = notesAt({{1.0f, 0, 0, 40}});
    CHECK(judgeInput(notes, {1.0, 3}).judgement == Judgement::Ignored); // another string
    CHECK(judgeInput(notes, {0.8, 0}).judgement == Judgement::Ignored); // 200 ms early
    CHECK_FALSE(notes[0].judged);
}

TEST_CASE("one input judges only the nearest matching note"){
    std::vector<PlayNote> notes = notesAt({{1.00f, 0, 0, 40}, {1.08f, 0, 0, 40}});
    JudgeResult r = judgeInput(notes, {1.07, 0});
    CHECK(r.notesHit == 1);
    CHECK_FALSE(notes[0].judged);
    CHECK(notes[1].hit);
}

TEST_CASE("instrument input matches by pitch, whatever the string"){
    std::vector<PlayNote> notes = notesAt({{1.0f, 0, 5, 45}}); // A2 played on the low E string
    CHECK(judgeInput(notes, {1.0, -1, 50}).judgement == Judgement::Ignored); // D3: wrong note
    CHECK(judgeInput(notes, {1.01, -1, 45}).judgement == Judgement::Perfect); // A2
}

TEST_CASE("a detected note of a chord counts the whole chord"){
    std::vector<PlayNote> notes = notesAt({{1.0f, 0, 3, 43}, {1.0f, 2, 2, 52}, {1.0f, 4, 1, 60}, {1.5f, 0, 0, 40}});
    JudgeResult r = judgeInput(notes, {1.01, -1, 52});
    CHECK(r.notesHit == 3);
    CHECK(notes[0].hit);
    CHECK(notes[2].hit);
    CHECK_FALSE(notes[3].judged); // a later note isn't part of the chord
}

TEST_CASE("notes left behind are missed"){
    std::vector<PlayNote> notes = notesAt({{1.0f, 0, 0, 40}, {1.5f, 0, 0, 40}, {3.0f, 0, 0, 40}});
    judgeInput(notes, {1.0, 0});
    CHECK(markMisses(notes, 1.55) == 0);  // 1.5 is only 50 ms ago: still hittable
    CHECK(markMisses(notes, 1.7) == 1);   // now it's too late
    CHECK(notes[1].judged);
    CHECK_FALSE(notes[1].hit);
    CHECK(notes[0].hit);                  // the hit one stays hit
    CHECK(markMisses(notes, 1.7) == 0);   // each miss is counted once
}

TEST_CASE("a part played on another instrument counts its notes in any octave"){
    // A guitar melody (E3, G3) played on a bass, an octave and two octaves down
    std::vector<PlayNote> notes = notesAt({{1.0f, 0, 0, 52}, {2.0f, 0, 3, 55}});
    PlayerInput low{1.0, -1, 40};
    CHECK(judgeInput(notes, low).judgement == Judgement::Ignored); // an octave down, on its own instrument: wrong
    low.anyOctave = true;
    CHECK(judgeInput(notes, low).judgement == Judgement::Perfect);
    PlayerInput wrong{2.0, -1, 30};                                 // not the note, in any octave
    wrong.anyOctave = true;
    CHECK(judgeInput(notes, wrong).judgement == Judgement::Ignored);
    PlayerInput lower{2.0, -1, 31};                                 // two octaves down
    lower.anyOctave = true;
    CHECK(judgeInput(notes, lower).judgement == Judgement::Perfect);
}
