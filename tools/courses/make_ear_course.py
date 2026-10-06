#!/usr/bin/env python3
"""Writes lahn's Ear training course (resources/courses/03-ear-training.course): you hear a note, and play it back
on your guitar, a few more notes at a time; then intervals by name. Change the plan below and run it again:

    python3 tools/courses/make_ear_course.py

A chapter of notes to play back goes:
  1. heard, and shown where it is (sound and place, together)
  2. by ear: the notes it could be outlined, which one is it?
  3. no slips
"""

import os

LETTERS = "CDEFGAB"
SEMITONES = [0, 2, 4, 5, 7, 9, 11]
NAMES = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"]
DEGREE_NAMES = {0: "1", 2: "2", 3: "b3", 4: "3", 5: "4", 7: "5", 9: "6", 10: "b7", 11: "7", 12: "8"}
INTERVAL_NAMES = {1: "minor 2nd", 2: "major 2nd", 3: "minor 3rd", 4: "major 3rd", 5: "perfect 4th", 6: "tritone", 7: "perfect 5th",
                  8: "minor 6th", 9: "major 6th", 10: "minor 7th", 11: "major 7th", 12: "octave"}


def note(pitch):
    return f"{NAMES[pitch % 12]}{pitch // 12 - 1}"


def listing(items):
    items = list(items)
    return items[0] if len(items) == 1 else ", ".join(items[:-1]) + " and " + items[-1]


def ear_drills(notes, string=None, reference=None, hold_where=True):
    """The three drills of a chapter of notes heard and played back"""
    common = ["type notes", "notes " + " ".join(notes), "show ear"]
    if string:
        common.append(f"strings {string}")
    if reference:
        common.append(f"reference {reference}")
    lines = []
    if hold_where:
        lines += ["drill Heard and shown"] + common + ["where yes", "count 6", "pass 5"]
    lines += ["drill By ear"] + common + ["count 8", "pass 7"]
    lines += ["drill No slips"] + common + ["count 8", "pass 8"]
    return lines


def degrees_chapter(title, root, degrees, string, text):
    pitches = [root + d for d in degrees]
    lines = [f"lesson {title}", f"text {text}"]
    lines.append(f"text The root, {NAMES[root % 12]}, plays first each time: hear the other note against it. "
                 f"The notes it could be are outlined on the {['low E', 'A', 'D', 'G', 'B', 'high E'][string - 1]} string.")
    lines += ear_drills([note(p) for p in pitches], string=string, reference=note(root))
    return lines


def interval_chapter(title, pool, text):
    lines = [f"lesson {title}", f"text {text}"]
    for label, direction in (("Going up", "up"), ("Going down", "down"), ("Played together", "together")):
        lines += [f"drill {label}", "type intervals", f"direction {direction}", "intervals " + " ".join(str(i) for i in pool),
                  f"start {len(pool)}", "goal 8"]
    return lines


def build():
    out = ["# lahn course: written by tools/courses/make_ear_course.py (change it there, and run it again)",
           "version 1",
           "title Ear training",
           "description lahn plays a note, you find it on your guitar and play it back: from two notes far apart to every note of a scale, then intervals by name.",
           "instrument guitar", ""]

    def level(title):
        out.extend(["", f"unit {title}", ""])

    def chapter(lines):
        out.extend(lines + [""])

    # Open strings, one more each chapter: far apart first
    level("Hear it, find it")
    opens = [("E2", "the low E string"), ("E4", "the high E string"), ("A2", "the A string"), ("G3", "the G string"),
             ("D3", "the D string"), ("B3", "the B string")]
    for count in range(2, len(opens) + 1):
        pool = [n for n, _ in opens[:count]]
        newest = opens[count - 1]
        if count == 2:
            title, text = "High or low", "lahn plays an open string: the low E or the high E. They're two octaves apart: listen, and play the one you hear."
        else:
            title, text = f"Add {newest[1]}", f"One more: {newest[1]}, open. Listen, then play back the one you hear."
        lines = [f"lesson {title}", f"text {text}",
                 "text The first drill shows you where it is as it plays: sound and place, together. Then by ear: every string it could be is outlined. Space plays it again."]
        lines += ear_drills(pool, hold_where=True)
        chapter(lines)

    # Scale degrees on one string, against the root played first
    level("One string, step by step")
    e = 64  # E4: the high E string, open
    for title, degrees, text in [
        ("The root and the fifth", [0, 7], "The open E, then either the same E or the B at the 7th fret: five steps up the E major scale."),
        ("1, 3 and 5", [0, 4, 7], "The notes of the E major chord: E, G# (fret 4) and B (fret 7)."),
        ("1 to 5", [0, 2, 4, 5, 7], "The first five notes of the E major scale, frets 0, 2, 4, 5 and 7."),
        ("The whole scale", [0, 2, 4, 5, 7, 9, 11, 12], "All eight, up to the E at the 12th fret."),
        ("1 and the octave", [0, 12], "The same note, an octave apart: the open E and the 12th fret."),
        ("The top of the scale", [7, 9, 11, 12], "5, 6, 7 and 8: B, C#, D# and E."),
    ]:
        chapter(degrees_chapter(title, e, degrees, 6, text))

    # The same in other keys, on other strings
    for key_name, root, string in [("A", 45, 2), ("D", 50, 3), ("G", 55, 4)]:
        level(f"In {key_name}, on the {['low E', 'A', 'D', 'G', 'B', 'high E'][string - 1]} string")
        for title, degrees, text in [
            (f"{key_name}: 1, 3 and 5", [0, 4, 7], f"The {key_name} major chord's notes, from the open string."),
            (f"{key_name}: 1 to 5", [0, 2, 4, 5, 7], f"The first five notes of {key_name} major."),
            (f"{key_name}: the whole scale", [0, 2, 4, 5, 7, 9, 11, 12], f"{key_name} major, up the string to its 12th fret."),
        ]:
            chapter(degrees_chapter(title, root, degrees, string, text))

    # Minor
    level("Minor")
    for title, degrees, text in [
        ("Major or minor third", [0, 3, 4], "The 3rd decides it: three frets up sounds minor, four sounds major."),
        ("A minor: 1, b3 and 5", [0, 3, 7], "The A minor chord's notes: A, C and E."),
        ("A minor: 1 to 5", [0, 2, 3, 5, 7], "The first five notes of A minor."),
        ("A minor: the whole scale", [0, 2, 3, 5, 7, 8, 10, 12], "All of A minor, up the A string."),
    ]:
        chapter(degrees_chapter(title, 45, degrees, 2, text))

    # Intervals by name, one more each chapter: very different sounds first
    level("Intervals by name")
    order = [7, 4, 12, 5, 3, 2, 9, 10, 8, 1, 11, 6]
    for count in range(2, len(order) + 1):
        pool = order[:count]
        newest = INTERVAL_NAMES[pool[-1]]
        text = (f"Two notes: how far apart? A {INTERVAL_NAMES[7]} or a {INTERVAL_NAMES[4]}." if count == 2
                else f"One more: the {newest}. Going up, going down, then both notes at once.")
        chapter(interval_chapter(f"{newest.capitalize()}" if count > 2 else "Fifth or third", pool, text))

    return "\n".join(out).rstrip() + "\n"


if __name__ == "__main__":
    here = os.path.dirname(os.path.abspath(__file__))
    path = os.path.join(here, "..", "..", "resources", "courses", "03-ear-training.course")
    text = build()
    with open(path, "w") as f:
        f.write(text)
    drills = sum(1 for line in text.splitlines() if line.startswith("drill "))
    chapters = sum(1 for line in text.splitlines() if line.startswith("lesson "))
    levels = sum(1 for line in text.splitlines() if line.startswith("unit "))
    print(f"{os.path.normpath(path)}: {levels} levels, {chapters} chapters, {drills} drills")
