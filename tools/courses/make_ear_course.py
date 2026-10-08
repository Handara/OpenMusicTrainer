#!/usr/bin/env python3
"""Writes lahn's Ear training courses, for guitar (resources/courses/03-ear-training.course), for bass
(03-ear-training-bass.course) and for piano (03-ear-training-piano.course): you hear a note, and play it back, a few more notes at a time; then intervals by
name. Learn shows the one for the instrument played. Change the plans below and run it again:

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


def ear_drills(notes, string=None, reference=None, hold_where=True, piano=False):
    """The three drills of a chapter of notes heard and played back"""
    common = ["type notes", "notes " + " ".join(notes), "show ear"] + (["instrument piano"] if piano else [])
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


def degrees_chapter(strings, title, root, degrees, string, text):
    pitches = [root + d for d in degrees]
    lines = [f"lesson {title}", f"text {text}"]
    lines.append(f"text The root, {NAMES[root % 12]}, plays first each time: hear the other note against it. "
                 f"The notes it could be are outlined on the {strings[string - 1]} string.")
    lines += ear_drills([note(p) for p in pitches], string=string, reference=note(root))
    return lines


def interval_chapter(title, pool, text):
    lines = [f"lesson {title}", f"text {text}"]
    for label, direction in (("Going up", "up"), ("Going down", "down"), ("Played together", "together")):
        lines += [f"drill {label}", "type intervals", f"direction {direction}", "intervals " + " ".join(str(i) for i in pool),
                  f"start {len(pool)}", "goal 8"]
    return lines


# Each instrument's plan: its open strings (far apart first), the string the scale degrees start on, the keys after
PLANS = {
    "guitar": {
        "strings": ["low E", "A", "D", "G", "B", "high E"],
        "opens": [("E2", "the low E string"), ("E4", "the high E string"), ("A2", "the A string"), ("G3", "the G string"),
                  ("D3", "the D string"), ("B3", "the B string")],
        "first_key": ("E", 64, 6),  # name, the root (MIDI), its string (1 = the lowest)
        "keys": [("A", 45, 2), ("D", 50, 3), ("G", 55, 4)],
        "minor": ("A", 45, 2),
    },
    "bass": {
        "strings": ["E", "A", "D", "G"],
        "opens": [("E1", "the E string"), ("G2", "the G string"), ("A1", "the A string"), ("D2", "the D string")],
        "first_key": ("G", 43, 4),
        "keys": [("D", 38, 3), ("A", 33, 2), ("E", 28, 1)],
        "minor": ("A", 33, 2),
    },
}


# The piano's: middle C's neighbours far apart first, then degrees of a scale on the keys, other keys, minor, intervals
def piano_degrees_chapter(title, root, degrees, text):
    pitches = [root + d for d in degrees]
    lines = [f"lesson {title}", f"text {text}",
             f"text {NAMES[root % 12]}, the root, plays first each time: hear the other note against it. The keys it could be are tinted."]
    lines += ear_drills([note(p) for p in pitches], reference=note(root), piano=True)
    return lines


def build_piano():
    out = ["# lahn course: written by tools/courses/make_ear_course.py (change it there, and run it again)",
           "version 1",
           "title Ear training",
           "description lahn plays a note, you find it on the keys and play it back: from two notes far apart to every note of a scale, then intervals by name.",
           "instrument piano", ""]

    def level(title):
        out.extend(["", f"unit {title}", ""])

    def chapter(lines):
        out.extend(lines + [""])

    level("Hear it, find it")
    keys = [("C4", "middle C"), ("C5", "the C above it"), ("G4", "the G between them"), ("C3", "the C below middle C"),
            ("E4", "the E above middle C"), ("A4", "the A above middle C")]
    for count in range(2, len(keys) + 1):
        pool = [n for n, _ in keys[:count]]
        newest = keys[count - 1]
        if count == 2:
            title = "High or low"
            text = "lahn plays a C: middle C, or the C an octave above. They're far apart: listen, and play the one you hear."
        else:
            title, text = f"Add {newest[1]}", f"One more: {newest[1]}. Listen, then play back the one you hear."
        lines = [f"lesson {title}", f"text {text}",
                 "text The first drill lights the key as it plays: sound and place, together. Then by ear: the keys it could be are tinted. Space plays it again."]
        lines += ear_drills(pool, piano=True)
        chapter(lines)

    level("C major, step by step")
    for title, degrees, text in [
        ("The root and the fifth", [0, 7], "Middle C, then either the same C or G, five white keys up: the 1st and 5th of C major."),
        ("1, 3 and 5", [0, 4, 7], "C, E and G: the notes of the C major chord, every other white key."),
        ("1 to 5", [0, 2, 4, 5, 7], "C, D, E, F, G: your right hand's five fingers."),
        ("The whole scale", [0, 2, 4, 5, 7, 9, 11, 12], "Every white key from middle C up to the next C."),
        ("1 and the octave", [0, 12], "The same note, an octave apart: middle C and the C above."),
        ("The top of the scale", [7, 9, 11, 12], "5, 6, 7 and 8: G, A, B and C."),
    ]:
        chapter(piano_degrees_chapter(title, 60, degrees, text))

    for key_name, key_root, black in [("G", 55, "F#, the one black key in it"), ("F", 53, "Bb, the one black key in it"), ("D", 62, "F# and C#, its two black keys")]:
        level(f"In {key_name} major")
        for title, degrees, text in [
            (f"{key_name}: 1, 3 and 5", [0, 4, 7], f"The {key_name} major chord's notes."),
            (f"{key_name}: 1 to 5", [0, 2, 4, 5, 7], f"The first five notes of {key_name} major."),
            (f"{key_name}: the whole scale", [0, 2, 4, 5, 7, 9, 11, 12], f"{key_name} major, with {black}."),
        ]:
            chapter(piano_degrees_chapter(title, key_root, degrees, text))

    level("Minor")
    for title, degrees, text in [
        ("Major or minor third", [0, 3, 4], "The 3rd decides it: three keys up (counting black ones) sounds minor, four sounds major."),
        ("A minor: 1, b3 and 5", [0, 3, 7], "A, C and E: the A minor chord's notes, all white keys."),
        ("A minor: 1 to 5", [0, 2, 3, 5, 7], "The first five notes of A minor."),
        ("A minor: the whole scale", [0, 2, 3, 5, 7, 8, 10, 12], "Every white key from A to A: A minor."),
    ]:
        chapter(piano_degrees_chapter(title, 57, degrees, text))

    level("Intervals by name")
    order = [7, 4, 12, 5, 3, 2, 9, 10, 8, 1, 11, 6]
    for count in range(2, len(order) + 1):
        pool = order[:count]
        newest = INTERVAL_NAMES[pool[-1]]
        text = (f"Two notes: how far apart? A {INTERVAL_NAMES[7]} or a {INTERVAL_NAMES[4]}." if count == 2
                else f"One more: the {newest}. Going up, going down, then both notes at once.")
        chapter(interval_chapter(f"{newest.capitalize()}" if count > 2 else "Fifth or third", pool, text))

    return "\n".join(out).rstrip() + "\n"


def build(instrument):
    plan = PLANS[instrument]
    strings = plan["strings"]
    out = ["# lahn course: written by tools/courses/make_ear_course.py (change it there, and run it again)",
           "version 1",
           "title Ear training",
           f"description lahn plays a note, you find it on your {instrument} and play it back: from two notes far apart to every note of a scale, then intervals by name.",
           f"instrument {instrument}", ""]

    def level(title):
        out.extend(["", f"unit {title}", ""])

    def chapter(lines):
        out.extend(lines + [""])

    # Open strings, one more each chapter: far apart first
    level("Hear it, find it")
    opens = plan["opens"]
    for count in range(2, len(opens) + 1):
        pool = [n for n, _ in opens[:count]]
        newest = opens[count - 1]
        if count == 2:
            title = "High or low"
            text = f"lahn plays an open string: {opens[0][1]} or {opens[1][1]}. They're far apart: listen, and play the one you hear."
        else:
            title, text = f"Add {newest[1]}", f"One more: {newest[1]}, open. Listen, then play back the one you hear."
        lines = [f"lesson {title}", f"text {text}",
                 "text The first drill shows you where it is as it plays: sound and place, together. Then by ear: every string it could be is outlined. Space plays it again."]
        lines += ear_drills(pool, hold_where=True)
        chapter(lines)

    # Scale degrees on one string, against the root played first
    name, root, string = plan["first_key"]
    level("One string, step by step")
    for title, degrees, text in [
        ("The root and the fifth", [0, 7], f"The open {name}, then either the same {name} or the note at the 7th fret: five steps up the {name} major scale."),
        ("1, 3 and 5", [0, 4, 7], f"The notes of the {name} major chord: the open string, the 4th fret and the 7th."),
        ("1 to 5", [0, 2, 4, 5, 7], f"The first five notes of the {name} major scale, frets 0, 2, 4, 5 and 7."),
        ("The whole scale", [0, 2, 4, 5, 7, 9, 11, 12], "All eight, up to the octave at the 12th fret."),
        ("1 and the octave", [0, 12], "The same note, an octave apart: the open string and the 12th fret."),
        ("The top of the scale", [7, 9, 11, 12], "5, 6, 7 and 8: frets 7, 9, 11 and 12."),
    ]:
        chapter(degrees_chapter(strings, title, root, degrees, string, text))

    # The same in other keys, on other strings
    for key_name, key_root, key_string in plan["keys"]:
        level(f"In {key_name}, on the {strings[key_string - 1]} string")
        for title, degrees, text in [
            (f"{key_name}: 1, 3 and 5", [0, 4, 7], f"The {key_name} major chord's notes, from the open string."),
            (f"{key_name}: 1 to 5", [0, 2, 4, 5, 7], f"The first five notes of {key_name} major."),
            (f"{key_name}: the whole scale", [0, 2, 4, 5, 7, 9, 11, 12], f"{key_name} major, up the string to its 12th fret."),
        ]:
            chapter(degrees_chapter(strings, title, key_root, degrees, key_string, text))

    # Minor
    name, root, string = plan["minor"]
    level("Minor")
    for title, degrees, text in [
        ("Major or minor third", [0, 3, 4], "The 3rd decides it: three frets up sounds minor, four sounds major."),
        (f"{name} minor: 1, b3 and 5", [0, 3, 7], f"The {name} minor chord's notes."),
        (f"{name} minor: 1 to 5", [0, 2, 3, 5, 7], f"The first five notes of {name} minor."),
        (f"{name} minor: the whole scale", [0, 2, 3, 5, 7, 8, 10, 12], f"All of {name} minor, up the {strings[string - 1]} string."),
    ]:
        chapter(degrees_chapter(strings, title, root, degrees, string, text))

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
    for instrument, file in (("guitar", "03-ear-training.course"), ("bass", "03-ear-training-bass.course"), ("piano", "03-ear-training-piano.course")):
        path = os.path.join(here, "..", "..", "resources", "courses", file)
        text = build_piano() if instrument == "piano" else build(instrument)
        with open(path, "w") as f:
            f.write(text)
        drills = sum(1 for line in text.splitlines() if line.startswith("drill "))
        chapters = sum(1 for line in text.splitlines() if line.startswith("lesson "))
        levels = sum(1 for line in text.splitlines() if line.startswith("unit "))
        print(f"{os.path.normpath(path)}: {levels} levels, {chapters} chapters, {drills} drills")
