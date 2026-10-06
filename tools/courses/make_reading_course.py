#!/usr/bin/env python3
"""Writes lahn's Reading music course (resources/courses/02-reading.course): hundreds of small drills, a note or
two at a time, in the same steps every time. Change the plan below and run it again:

    python3 tools/courses/make_reading_course.py

Each chapter brings in its new notes the same way:
  1. the new note alone, the neck showing where it is
  2. with the other notes of its string, still shown
  3. the same with no hints (a slip shows where it was)
  4. mixed with what's been learned so far
  5. no slips allowed
Then come levels read to a beat (the timed reading drill), more and more rhythms, then key signatures.
"""

import os

GUITAR = [40, 45, 50, 55, 59, 64]  # MIDI, lowest string first
STRING_NAMES = ["low E", "A", "D", "G", "B", "high E"]
LETTERS = "CDEFGAB"
SEMITONES = [0, 2, 4, 5, 7, 9, 11]


def parse(name):
    """'F#4' -> (MIDI, letter index, octave)"""
    letter = LETTERS.index(name[0])
    rest = name[1:]
    alter = 0
    if rest.startswith("#"):
        alter, rest = 1, rest[1:]
    elif rest.startswith("b"):
        alter, rest = -1, rest[1:]
    octave = int(rest)
    return (octave + 1) * 12 + SEMITONES[letter] + alter, letter, octave


def spoken(name):
    """'F#4' -> 'F sharp', 'Bb3' -> 'B flat'"""
    base = name[0]
    if len(name) > 1 and name[1] == "#":
        return base + " sharp"
    if len(name) > 1 and name[1] == "b":
        return base + " flat"
    return base


def ordinal(n):
    if 11 <= n % 100 <= 13:
        return f"{n}th"
    return f"{n}{ {1: 'st', 2: 'nd', 3: 'rd'}.get(n % 10, 'th') }"


def place(name):
    """Where lahn plays it: its lowest fret on any string (as core/notequiz placeNotes does)"""
    pitch = parse(name)[0]
    best = None
    for string, open_pitch in enumerate(GUITAR):
        fret = pitch - open_pitch
        if 0 <= fret <= 24 and (best is None or fret < best[1]):
            best = (string, fret)
    return best


def place_text(name):
    string, fret = place(name)
    if fret == 0:
        return f"the open {STRING_NAMES[string]} string"
    return f"the {ordinal(fret)} fret of the {STRING_NAMES[string]} string"


def staff_text(name):
    """Where it's written (guitar music: an octave above how it sounds) on the treble staff"""
    _, letter, octave = parse(name)
    step = (octave + 1) * 7 + letter - (4 * 7 + 2)  # diatonic steps above the bottom line (written E4)
    lines = ["the bottom line", "the second line", "the middle line", "the fourth line", "the top line"]
    spaces = ["the bottom space", "the second space", "the third space", "the top space"]
    if 0 <= step <= 8:
        return "on " + lines[step // 2] if step % 2 == 0 else "in " + spaces[step // 2]
    if step == 9:
        return "just above the staff"
    if step == -1:
        return "just below the staff"
    if step > 9:
        ledger = (step - 8) // 2
        return f"on the {ordinal(ledger)} ledger line above the staff" if step % 2 == 0 else f"above the {ordinal(ledger)} ledger line over the staff"
    ledger = (-step) // 2
    return f"on the {ordinal(ledger)} ledger line below the staff" if step % 2 == 0 else f"below the {ordinal(ledger)} ledger line under the staff"


def listing(names):
    names = [spoken(n) for n in names]
    return names[0] if len(names) == 1 else ", ".join(names[:-1]) + " and " + names[-1]


def notes_drill(name, notes, where, count, passing, key=None):
    lines = [f"drill {name}", "type notes", "notes " + " ".join(notes), "show staff"]
    if where:
        lines.append("where yes")
    lines += [f"count {count}", f"pass {passing}"]
    if key:
        lines.append(f"key {key}")
    return lines


def note_chapter(title, new, string_notes, known, key=None, intro=None):
    """A chapter bringing in `new`, with the other notes of its string and what's known so far"""
    lines = [f"lesson {title}"]
    if intro:
        lines.append(f"text {intro}")
    for note in new:
        lines.append(f"text {spoken(note)} sits {staff_text(note)}. On the guitar it's {place_text(note)}.")
    group = list(dict.fromkeys(string_notes + new))
    mixed = list(dict.fromkeys(known + new))
    if len(new) == 1:
        lines += notes_drill(f"{spoken(new[0])} alone", new, True, 4, 3, key)
    if len(group) > len(new):
        lines += notes_drill(f"{listing(group)}, shown where", group, True, 8, 7, key)
    if len(group) > 1:
        lines += notes_drill(f"{listing(group)}, on your own", group, False, 8, 7, key)
    if len(mixed) > len(group):
        recent = mixed[-9:]  # the last notes learned: enough to mix, not so many it's a lottery
        lines += notes_drill(f"Mixed with what you know", recent, False, 12, 11, key)
        lines += notes_drill(f"No slips", recent, False, 12, 12, key)
    else:
        lines += notes_drill(f"No slips", group, False, 6 if len(group) == 1 else 10, 6 if len(group) == 1 else 10, key)
    return lines


def review_chapter(title, notes, text, key=None):
    lines = [f"lesson {title}", f"text {text}"]
    lines += notes_drill("Shown where", notes, True, 12, 11, key)
    lines += notes_drill("On your own", notes, False, 16, 15, key)
    lines += notes_drill("No slips", notes, False, 16, 16, key)
    return lines


def timed_chapter(title, text, strings, frets, cells, tempos, key="C major", leap=2):
    """Reading to a beat (the timed reading drill): the same melody kind at a few tempos, each passed clean once"""
    tonic, mode = key.split()
    scale = "major" if mode == "major" else "minor"
    lines = [f"lesson {title}", f"text {text}"]
    for label, start, goal in tempos:
        lines += [f"drill {label}", "type reading", f"key {tonic}", f"scale {scale}", f"frets {frets[0]} {frets[1]}",
                  "strings " + " ".join(str(s) for s in strings), f"leap {leap}", "cells " + " ".join(cells), "bars 2",
                  f"tempo {start} {goal} 5", "goal 1"]
    return lines


SLOW = [("Slowly", 50, 60), ("Steady", 60, 75), ("Moving on", 75, 95)]


def build():
    out = ["# lahn course: written by tools/courses/make_reading_course.py (change it there, and run it again)",
           "version 1",
           "title Reading music",
           "description From your first note on the staff to reading melodies to a beat, in every key: a note or two at a time.",
           "instrument guitar", ""]
    known = []

    def level(title):
        out.extend(["", f"unit {title}", ""])

    def chapter(lines):
        out.extend(lines + [""])

    # The open strings, one at a time, top to bottom: its notes, one chapter each
    plan = [
        ("The high E string", [["E4"], ["F4"], ["G4"]], ["E4", "F4", "G4"],
         "Music is written on five lines, the staff. The higher a note sits, the higher it sounds. The little 8 under the clef says guitar sounds an octave lower than written."),
        ("The B string", [["B3"], ["C4"], ["D4"]], ["B3", "C4", "D4"], None),
        ("The G string", [["G3"], ["A3"]], ["G3", "A3"], None),
        ("The D string", [["D3"], ["E3"], ["F3"]], ["D3", "E3", "F3"], "Down into the ledger lines: the short lines under the staff, each one more step down."),
        ("The A string", [["A2"], ["B2"], ["C3"]], ["A2", "B2", "C3"], None),
        ("The low E string", [["E2"], ["F2"], ["G2"]], ["E2", "F2", "G2"], None),
    ]
    for title, steps, string_notes, intro in plan:
        level(title)
        for i, new in enumerate(steps):
            chapter(note_chapter(f"{title}: {listing(new)}", new, string_notes[:string_notes.index(new[-1]) + 1],
                                 known, intro=intro if i == 0 else None))
            known = list(dict.fromkeys(known + new))
        chapter(review_chapter(f"{title}: all of it", list(dict.fromkeys(known[-9:])),
                               f"{listing(string_notes)}, and the strings before: read them as they come."))

    # Every natural note in the open position, in groups that cross the strings
    level("The whole open position")
    groups = [("The top three strings", ["G3", "A3", "B3", "C4", "D4", "E4", "F4", "G4"]),
              ("The middle strings", ["D3", "E3", "F3", "G3", "A3", "B3", "C4"]),
              ("The bottom three strings", ["E2", "F2", "G2", "A2", "B2", "C3", "D3", "E3", "F3"]),
              ("Low and high", ["E2", "G2", "C3", "F3", "A3", "D4", "G4"]),
              ("Everything", ["E2", "F2", "G2", "A2", "B2", "C3", "D3", "E3", "F3", "G3", "A3", "B3", "C4", "D4", "E4", "F4", "G4"])]
    for title, notes in groups:
        chapter(review_chapter(title, notes, f"{len(notes)} notes, from {spoken(notes[0])} to {spoken(notes[-1])}."))

    # Up the high E string, into the ledger lines above
    level("Above the staff")
    high_string = ["E4", "F4", "G4"]
    for new in (["A4"], ["B4"], ["C5"]):
        high_string = high_string + new
        chapter(note_chapter(f"Above the staff: {listing(new)}", new, high_string, known))
        known = list(dict.fromkeys(known + new))

    # Sharps and flats: what they do, then the ones that come up most
    level("Sharps and flats")
    accidentals = [
        (["F#4"], ["E4", "F4", "G4"], "A sharp sign raises a note a fret, a flat lowers it a fret. F sharp is one fret above F."),
        (["C#4"], ["B3", "C4", "D4"], None),
        (["G#3"], ["G3", "A3"], None),
        (["Bb3"], ["A3", "B3"], "Bb is B flat: a fret below B."),
        (["Eb4"], ["D4", "E4"], None),
        (["F#3"], ["E3", "F3", "G3"], None),
        (["C#3"], ["B2", "C3", "D3"], None),
        (["F#2"], ["E2", "F2", "G2"], None),
    ]
    for new, around, intro in accidentals:
        chapter(note_chapter(f"{listing(new)} on the {STRING_NAMES[place(new[0])[0]]} string", new, around, known, intro=intro))
        known = list(dict.fromkeys(known + new))

    # Key signatures: the sharps or flats written once, at the start
    level("Key signatures")
    keys = [
        ("G major", "One sharp, F#: every F is played F sharp, though no sign stands by it.", ["D3", "E3", "F#3", "G3", "A3", "B3", "C4", "D4", "E4", "F#4", "G4"]),
        ("D major", "Two sharps, F# and C#.", ["D3", "E3", "F#3", "G3", "A3", "B3", "C#4", "D4", "E4", "F#4", "G4"]),
        ("F major", "One flat, Bb: every B is played B flat.", ["C3", "D3", "E3", "F3", "G3", "A3", "Bb3", "C4", "D4", "E4", "F4"]),
        ("A major", "Three sharps: F#, C#, G#.", ["A2", "B2", "C#3", "E3", "F#3", "G#3", "A3", "B3", "C#4", "E4", "F#4"]),
        ("Bb major", "Two flats: Bb, Eb.", ["Bb2", "C3", "D3", "Eb3", "F3", "G3", "A3", "Bb3", "C4", "D4", "Eb4", "F4"]),
        ("E major", "Four sharps: F#, C#, G#, D#.", ["E2", "F#2", "G#2", "B2", "C#3", "E3", "F#3", "G#3", "B3", "C#4", "E4"]),
    ]
    for key, text, notes in keys:
        chapter(review_chapter(f"In {key}", notes, text, key=key))

    # To a beat: the timed reading drill, a little more each chapter
    level("Reading to a beat")
    timed = [
        ("Quarter notes, top two strings", "Now the notes come on a beat, a note on each one. A bar of clicks counts you in.", [5, 6], (0, 3), ["quarter"]),
        ("Quarter notes, top three strings", "One more string.", [4, 5, 6], (0, 3), ["quarter"]),
        ("Rests", "A rest is a beat with no note: let the string be quiet, and count it.", [4, 5, 6], (0, 3), ["quarter", "rest"]),
        ("Quarter notes, four strings", "Down to the D string.", [3, 4, 5, 6], (0, 3), ["quarter", "rest"]),
        ("Eighth notes", "Two notes on a beat: count 1 and 2 and.", [4, 5, 6], (0, 3), ["quarter", "eighths"]),
        ("Eighths, four strings", "The same, a string lower.", [3, 4, 5, 6], (0, 3), ["quarter", "eighths", "rest"]),
        ("Off the beat", "An eighth rest, then a note: the and of the beat.", [4, 5, 6], (0, 3), ["quarter", "offbeat", "rest"]),
        ("Dotted rhythms", "A long note and a short one: a dotted eighth and a sixteenth.", [4, 5, 6], (0, 3), ["quarter", "dotted"]),
        ("All six strings", "The whole open position, to a beat.", [1, 2, 3, 4, 5, 6], (0, 3), ["quarter", "eighths", "rest"]),
        ("Triplets", "Three notes on a beat.", [4, 5, 6], (0, 3), ["quarter", "triplets"]),
        ("Sixteenths", "Four notes on a beat, slowly.", [5, 6], (0, 3), ["quarter", "sixteenths"]),
    ]
    for title, text, strings, frets, cells in timed:
        chapter(timed_chapter(title, text, strings, frets, cells, SLOW))

    level("Keys to a beat")
    for key in ["G major", "F major", "D major", "A minor", "E minor"]:
        chapter(timed_chapter(f"{key}, to a beat", f"Melodies in {key}, read to a beat.", [3, 4, 5, 6], (0, 4), ["quarter", "eighths", "rest"], SLOW, key=key))

    return "\n".join(out).rstrip() + "\n"


if __name__ == "__main__":
    here = os.path.dirname(os.path.abspath(__file__))
    path = os.path.join(here, "..", "..", "resources", "courses", "02-reading.course")
    text = build()
    with open(path, "w") as f:
        f.write(text)
    drills = sum(1 for line in text.splitlines() if line.startswith("drill "))
    chapters = sum(1 for line in text.splitlines() if line.startswith("lesson "))
    levels = sum(1 for line in text.splitlines() if line.startswith("unit "))
    print(f"{os.path.normpath(path)}: {levels} levels, {chapters} chapters, {drills} drills")
