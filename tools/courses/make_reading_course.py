#!/usr/bin/env python3
"""Writes hardthz's Reading music courses, one for guitar (resources/courses/02-reading.course, the treble clef) and one
for bass (02-reading-bass.course, the bass clef): hundreds of small drills, a note or two at a time, in the same steps
every time. Learn shows the one for the instrument played. Change the plans below and run it again:

    python3 tools/courses/make_reading_course.py

Every drill is read to a beat: forty notes at random on a metronome (a bar counting in), 35 of them right to pass,
from 100 bpm and faster at each clean pass. Each chapter brings in its new notes the same way (nothing to learn by
heart):
  1. the new note alone, the neck lighting where it is
  2. with the other notes of its string, still lit
  3. the same with no neck
  4. mixed with what's been learned so far
Then come levels of melodies read to a beat, more and more rhythms, then key signatures.
"""

import os

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
    if len(name) > 1 and name[1] == "#":
        return name[0] + " sharp"
    if len(name) > 1 and name[1] == "b":
        return name[0] + " flat"
    return name[0]


def ordinal(n):
    if 11 <= n % 100 <= 13:
        return f"{n}th"
    return f"{n}{ {1: 'st', 2: 'nd', 3: 'rd'}.get(n % 10, 'th') }"


def listing(names):
    names = [spoken(n) for n in names]
    return names[0] if len(names) == 1 else ", ".join(names[:-1]) + " and " + names[-1]


class Instrument:
    def __init__(self, name, tuning, string_names, clef_bottom, clef_text):
        self.name = name
        self.tuning = tuning              # MIDI, lowest string first
        self.string_names = string_names  # as hardthz says them (core/notequiz notePlaceText)
        self.clef_bottom = clef_bottom    # the staff's bottom line, as written (an octave above how it sounds)
        self.clef_text = clef_text

    def place(self, name):
        """Where hardthz plays it: its lowest fret on any string (as core/notequiz placeNotes does)"""
        pitch = parse(name)[0]
        best = None
        for string, open_pitch in enumerate(self.tuning):
            fret = pitch - open_pitch
            if 0 <= fret <= 24 and (best is None or fret < best[1]):
                best = (string, fret)
        return best

    def place_text(self, name):
        string, fret = self.place(name)
        if fret == 0:
            return f"the open {self.string_names[string]} string"
        return f"the {ordinal(fret)} fret of the {self.string_names[string]} string"

    def staff_text(self, name):
        """Where it's written on the staff (an octave above how it sounds, as guitar and bass music is)"""
        _, letter, octave = parse(name)
        bottom_letter, bottom_octave = LETTERS.index(self.clef_bottom[0]), int(self.clef_bottom[1:])
        step = (octave + 1) * 7 + letter - (bottom_octave * 7 + bottom_letter)  # diatonic steps above the bottom line
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


GUITAR = Instrument("guitar", [40, 45, 50, 55, 59, 64], ["low E", "A", "D", "G", "B", "high E"], "E4",
                    "Music is written on five lines, the staff. The higher a note sits, the higher it sounds. The little 8 under the clef says guitar sounds an octave lower than written.")
BASS = Instrument("bass", [28, 33, 38, 43], ["E", "A", "D", "G"], "G2",
                  "Music is written on five lines, the staff. The higher a note sits, the higher it sounds. Bass music is written in the bass clef, the curl with two dots, and sounds an octave lower than written.")


def note_chapter(instrument, title, new, string_notes, known, key=None, intro=None):
    """A chapter bringing in `new`, with the other notes of its string and what's known so far"""
    lines = [f"lesson {title}"]
    if intro:
        lines.append(f"text {intro}")
    for note in new:
        lines.append(f"text {spoken(note)} sits {instrument.staff_text(note)}. On the {instrument.name} it's {instrument.place_text(note)}.")
    group = list(dict.fromkeys(string_notes + new))
    mixed = list(dict.fromkeys(known + new))
    if len(new) == 1:
        lines += beat_drill(instrument, f"{spoken(new[0])} alone", new, True, key)
    if len(group) > len(new):
        lines += beat_drill(instrument, f"{listing(group)}, shown where", group, True, key)
    lines += beat_drill(instrument, f"{listing(group)}, on your own", group, False, key)
    if len(mixed) > len(group):
        recent = mixed[-9:]  # the last notes learned: enough to mix, not so many it's a lottery
        lines += beat_drill(instrument, "Mixed with what you know", recent, False, key)
    return lines


def beat_drill(instrument, name, notes, where, key=None):
    """Notes at random, read to a metronome (where: the neck lighting each one): a little faster each clean pass"""
    lines = [f"drill {name}", "type reading", "notes " + " ".join(notes)]
    if where:
        lines.append("where yes")
    lines += ["cells quarter", f"bars {BEAT_BARS}", BEAT_TEMPO, BEAT_CHALLENGE, f"pass {BEAT_PASS}", "goal 1"]
    if key:
        tonic, mode = key.split()
        lines += [f"key {tonic}", f"scale {'major' if mode == 'major' else 'minor'}"]
    if instrument is BASS:
        lines.append("tuning " + " ".join(str(p) for p in BASS.tuning))
    return lines


def review_chapter(instrument, title, notes, text, key=None):
    lines = [f"lesson {title}", f"text {text}"]
    lines += beat_drill(instrument, "Shown where", notes, True, key)
    lines += beat_drill(instrument, "On your own", notes, False, key)
    return lines


def timed_chapter(instrument, title, text, strings, frets, cells, tempos, key="C major", leap=2):
    """Reading to a beat (the timed reading drill): the same melody kind at a few tempos, each passed clean once"""
    tonic, mode = key.split()
    scale = "major" if mode == "major" else "minor"
    lines = [f"lesson {title}", f"text {text}"]
    for label, start, goal in tempos:
        lines += [f"drill {label}", "type reading", f"key {tonic}", f"scale {scale}", f"frets {frets[0]} {frets[1]}",
                  "strings " + " ".join(str(s) for s in strings), f"leap {leap}", "cells " + " ".join(cells), f"bars {BEAT_BARS}",
                  f"tempo {start} {goal} 5", f"pass {BEAT_PASS}", "goal 1"]
        if instrument is BASS:
            lines.append("tuning " + " ".join(str(p) for p in BASS.tuning))
    return lines


BEAT_BARS = 10  # a run to a beat: ten bars, forty quarter notes
BEAT_PASS = 87  # 35 of the 40 notes (87.5%) pass it
BEAT_TEMPO = "tempo 90 120 5"  # notes at random: from 90 bpm, up to 120
BEAT_CHALLENGE = "challenge 100"  # passed with a clean pass at 100 bpm: slower is practice
SLOW = [("Slowly", 60, 80), ("Steady", 75, 100), ("Moving on", 90, 120)]

# What each instrument's course goes through: its open strings, top to bottom, a note a chapter; then what follows
PLANS = {
    "guitar": {
        "strings": [
            ("The high E string", [["E4"], ["F4"], ["G4"]], ["E4", "F4", "G4"], GUITAR.clef_text),
            ("The B string", [["B3"], ["C4"], ["D4"]], ["B3", "C4", "D4"], None),
            ("The G string", [["G3"], ["A3"]], ["G3", "A3"], None),
            ("The D string", [["D3"], ["E3"], ["F3"]], ["D3", "E3", "F3"], "Down into the ledger lines: the short lines under the staff, each one more step down."),
            ("The A string", [["A2"], ["B2"], ["C3"]], ["A2", "B2", "C3"], None),
            ("The low E string", [["E2"], ["F2"], ["G2"]], ["E2", "F2", "G2"], None),
        ],
        "groups": [("The top three strings", ["G3", "A3", "B3", "C4", "D4", "E4", "F4", "G4"]),
                   ("The middle strings", ["D3", "E3", "F3", "G3", "A3", "B3", "C4"]),
                   ("The bottom three strings", ["E2", "F2", "G2", "A2", "B2", "C3", "D3", "E3", "F3"]),
                   ("Low and high", ["E2", "G2", "C3", "F3", "A3", "D4", "G4"]),
                   ("Everything", ["E2", "F2", "G2", "A2", "B2", "C3", "D3", "E3", "F3", "G3", "A3", "B3", "C4", "D4", "E4", "F4", "G4"])],
        "high": (["E4", "F4", "G4"], [["A4"], ["B4"], ["C5"]]),
        "accidentals": [
            (["F#4"], ["E4", "F4", "G4"], "A sharp sign raises a note a fret, a flat lowers it a fret. F sharp is one fret above F."),
            (["C#4"], ["B3", "C4", "D4"], None),
            (["G#3"], ["G3", "A3"], None),
            (["Bb3"], ["A3", "B3"], "Bb is B flat: a fret below B."),
            (["Eb4"], ["D4", "E4"], None),
            (["F#3"], ["E3", "F3", "G3"], None),
            (["C#3"], ["B2", "C3", "D3"], None),
            (["F#2"], ["E2", "F2", "G2"], None),
        ],
        "keys": [
            ("G major", "One sharp, F#: every F is played F sharp, though no sign stands by it.", ["D3", "E3", "F#3", "G3", "A3", "B3", "C4", "D4", "E4", "F#4", "G4"]),
            ("D major", "Two sharps, F# and C#.", ["D3", "E3", "F#3", "G3", "A3", "B3", "C#4", "D4", "E4", "F#4", "G4"]),
            ("F major", "One flat, Bb: every B is played B flat.", ["C3", "D3", "E3", "F3", "G3", "A3", "Bb3", "C4", "D4", "E4", "F4"]),
            ("A major", "Three sharps: F#, C#, G#.", ["A2", "B2", "C#3", "E3", "F#3", "G#3", "A3", "B3", "C#4", "E4", "F#4"]),
            ("Bb major", "Two flats: Bb, Eb.", ["Bb2", "C3", "D3", "Eb3", "F3", "G3", "A3", "Bb3", "C4", "D4", "Eb4", "F4"]),
            ("E major", "Four sharps: F#, C#, G#, D#.", ["E2", "F#2", "G#2", "B2", "C#3", "E3", "F#3", "G#3", "B3", "C#4", "E4"]),
        ],
        "timed": [
            ("Quarter notes, top two strings", "Now the notes come on a beat, a note on each one. A bar of clicks counts you in.", [5, 6], ["quarter"]),
            ("Quarter notes, top three strings", "One more string.", [4, 5, 6], ["quarter"]),
            ("Rests", "A rest is a beat with no note: let the string be quiet, and count it.", [4, 5, 6], ["quarter", "rest"]),
            ("Quarter notes, four strings", "Down to the D string.", [3, 4, 5, 6], ["quarter", "rest"]),
            ("Eighth notes", "Two notes on a beat: count 1 and 2 and.", [4, 5, 6], ["quarter", "eighths"]),
            ("Eighths, four strings", "The same, a string lower.", [3, 4, 5, 6], ["quarter", "eighths", "rest"]),
            ("Off the beat", "An eighth rest, then a note: the and of the beat.", [4, 5, 6], ["quarter", "offbeat", "rest"]),
            ("Dotted rhythms", "A long note and a short one: a dotted eighth and a sixteenth.", [4, 5, 6], ["quarter", "dotted"]),
            ("All six strings", "The whole open position, to a beat.", [1, 2, 3, 4, 5, 6], ["quarter", "eighths", "rest"]),
            ("Triplets", "Three notes on a beat.", [4, 5, 6], ["quarter", "triplets"]),
            ("Sixteenths", "Four notes on a beat, slowly.", [5, 6], ["quarter", "sixteenths"]),
        ],
        "timed_frets": (0, 3),
        "timed_keys_strings": [3, 4, 5, 6],
    },
    "bass": {
        "strings": [
            ("The G string", [["G2"], ["A2"], ["B2"], ["C3"]], ["G2", "A2", "B2", "C3"], BASS.clef_text),
            ("The D string", [["D2"], ["E2"], ["F2"]], ["D2", "E2", "F2"], None),
            ("The A string", [["A1"], ["B1"], ["C2"]], ["A1", "B1", "C2"], None),
            ("The E string", [["E1"], ["F1"], ["G1"]], ["E1", "F1", "G1"], "Down into the ledger lines: the short lines under the staff, each one more step down."),
        ],
        "groups": [("The top two strings", ["D2", "E2", "F2", "G2", "A2", "B2", "C3"]),
                   ("The bottom two strings", ["E1", "F1", "G1", "A1", "B1", "C2"]),
                   ("The middle strings", ["A1", "B1", "C2", "D2", "E2", "F2"]),
                   ("Low and high", ["E1", "G1", "C2", "F2", "A2", "C3"]),
                   ("Everything", ["E1", "F1", "G1", "A1", "B1", "C2", "D2", "E2", "F2", "G2", "A2", "B2", "C3"])],
        "high": (["G2", "A2", "B2", "C3"], [["D3"], ["E3"], ["F3"], ["G3"]]),
        "accidentals": [
            (["F#1"], ["E1", "F1", "G1"], "A sharp sign raises a note a fret, a flat lowers it a fret. F sharp is one fret above F."),
            (["C#2"], ["B1", "C2", "D2"], None),
            (["Bb1"], ["A1", "B1"], "Bb is B flat: a fret below B."),
            (["Eb2"], ["D2", "E2"], None),
            (["F#2"], ["E2", "F2", "G2"], None),
            (["G#2"], ["G2", "A2"], None),
            (["Bb2"], ["A2", "B2"], None),
            (["G#1"], ["G1", "A1"], None),
        ],
        "keys": [
            ("G major", "One sharp, F#: every F is played F sharp, though no sign stands by it.", ["G1", "A1", "B1", "C2", "D2", "E2", "F#2", "G2", "A2", "B2", "C3"]),
            ("D major", "Two sharps, F# and C#.", ["A1", "B1", "C#2", "D2", "E2", "F#2", "G2", "A2", "B2", "C#3"]),
            ("F major", "One flat, Bb: every B is played B flat.", ["F1", "G1", "A1", "Bb1", "C2", "D2", "E2", "F2", "G2", "A2", "Bb2", "C3"]),
            ("A major", "Three sharps: F#, C#, G#.", ["E1", "F#1", "G#1", "A1", "B1", "C#2", "D2", "E2", "F#2", "G#2", "A2"]),
            ("Bb major", "Two flats: Bb, Eb.", ["F1", "G1", "A1", "Bb1", "C2", "D2", "Eb2", "F2", "G2", "A2", "Bb2"]),
            ("E major", "Four sharps: F#, C#, G#, D#.", ["E1", "F#1", "G#1", "A1", "B1", "C#2", "D#2", "E2", "F#2", "G#2", "B2"]),
        ],
        "timed": [
            ("Quarter notes, top two strings", "Now the notes come on a beat, a note on each one. A bar of clicks counts you in.", [3, 4], ["quarter"]),
            ("Quarter notes, three strings", "One more string.", [2, 3, 4], ["quarter"]),
            ("Rests", "A rest is a beat with no note: let the string be quiet, and count it.", [2, 3, 4], ["quarter", "rest"]),
            ("All four strings", "Down to the E string.", [1, 2, 3, 4], ["quarter", "rest"]),
            ("Eighth notes", "Two notes on a beat: count 1 and 2 and.", [2, 3, 4], ["quarter", "eighths"]),
            ("Eighths, four strings", "The same, a string lower.", [1, 2, 3, 4], ["quarter", "eighths", "rest"]),
            ("Off the beat", "An eighth rest, then a note: the and of the beat.", [2, 3, 4], ["quarter", "offbeat", "rest"]),
            ("Dotted rhythms", "A long note and a short one: a dotted eighth and a sixteenth.", [2, 3, 4], ["quarter", "dotted"]),
            ("Triplets", "Three notes on a beat.", [3, 4], ["quarter", "triplets"]),
            ("Sixteenths", "Four notes on a beat, slowly.", [3, 4], ["quarter", "sixteenths"]),
        ],
        "timed_frets": (0, 5),
        "timed_keys_strings": [1, 2, 3, 4],
    },
}


def build(instrument):
    plan = PLANS[instrument.name]
    out = ["# hardthz course: written by tools/courses/make_reading_course.py (change it there, and run it again)",
           "version 1",
           "title Reading music",
           "description From your first note on the staff to reading melodies to a beat, in every key: a note or two at a time.",
           f"instrument {instrument.name}", ""]
    known = []

    def level(title):
        out.extend(["", f"unit {title}", ""])

    def chapter(lines):
        out.extend(lines + [""])

    # The open strings, one at a time, top to bottom: its notes, one chapter each
    for title, steps, string_notes, intro in plan["strings"]:
        level(title)
        for i, new in enumerate(steps):
            chapter(note_chapter(instrument, f"{title}: {listing(new)}", new, string_notes[:string_notes.index(new[-1]) + 1], known,
                                 intro=intro if i == 0 else None))
            known = list(dict.fromkeys(known + new))
        chapter(review_chapter(instrument, f"{title}: all of it", list(dict.fromkeys(known[-9:])),
                               f"{listing(string_notes)}, and the strings before: read them as they come."))

    # Every natural note in the open position, in groups that cross the strings
    level("The whole open position")
    for title, notes in plan["groups"]:
        chapter(review_chapter(instrument, title, notes, f"{len(notes)} notes, from {spoken(notes[0])} to {spoken(notes[-1])}."))

    # Up the top string, into the ledger lines above
    level("Above the staff")
    top_string, highs = plan["high"]
    for new in highs:
        top_string = top_string + new
        chapter(note_chapter(instrument, f"Above the staff: {listing(new)}", new, top_string[-4:], known))
        known = list(dict.fromkeys(known + new))

    # Sharps and flats: what they do, then the ones that come up most
    level("Sharps and flats")
    for new, around, intro in plan["accidentals"]:
        chapter(note_chapter(instrument, f"{listing(new)} on the {instrument.string_names[instrument.place(new[0])[0]]} string", new, around, known, intro=intro))
        known = list(dict.fromkeys(known + new))

    # Key signatures: the sharps or flats written once, at the start
    level("Key signatures")
    for key, text, notes in plan["keys"]:
        chapter(review_chapter(instrument, f"In {key}", notes, text, key=key))

    # To a beat: the timed reading drill, a little more each chapter
    level("Reading to a beat")
    for title, text, strings, cells in plan["timed"]:
        chapter(timed_chapter(instrument, title, text, strings, plan["timed_frets"], cells, SLOW))

    level("Keys to a beat")
    for key in ["G major", "F major", "D major", "A minor", "E minor"]:
        chapter(timed_chapter(instrument, f"{key}, to a beat", f"Melodies in {key}, read to a beat.", plan["timed_keys_strings"],
                              (0, max(4, plan["timed_frets"][1])), ["quarter", "eighths", "rest"], SLOW, key=key))

    return "\n".join(out).rstrip() + "\n"


if __name__ == "__main__":
    here = os.path.dirname(os.path.abspath(__file__))
    for instrument, file in ((GUITAR, "02-reading.course"), (BASS, "02-reading-bass.course")):
        path = os.path.join(here, "..", "..", "resources", "courses", file)
        text = build(instrument)
        with open(path, "w") as f:
            f.write(text)
        drills = sum(1 for line in text.splitlines() if line.startswith("drill "))
        chapters = sum(1 for line in text.splitlines() if line.startswith("lesson "))
        levels = sum(1 for line in text.splitlines() if line.startswith("unit "))
        print(f"{os.path.normpath(path)}: {levels} levels, {chapters} chapters, {drills} drills")
