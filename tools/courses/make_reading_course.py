#!/usr/bin/env python3
"""Writes lahn's Reading music courses, one for guitar (resources/courses/02-reading.course, the treble clef), one
for bass (02-reading-bass.course, the bass clef) and one for piano (02-reading-piano.course, both clefs): hundreds of small drills, a note or two at a time, in the same steps
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

from coursev2 import to_v2

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
        self.string_names = string_names  # as lahn says them (core/notequiz notePlaceText)
        self.clef_bottom = clef_bottom    # the staff's bottom line, as written (an octave above how it sounds)
        self.clef_text = clef_text

    def place(self, name):
        """Where lahn plays it: its lowest fret on any string (as core/notequiz placeNotes does)"""
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


# Rhythm, learned alongside the notes: a rung each note chapter (its words, and an example on the chapter's own note,
# N), each bringing in a rhythm cell (core/rhythm) that the chapter's challenges read from then on
RHYTHM_LADDER = [
    ("the beat", "quarter",
     "A filled note with a stem is a **quarter note**: it lasts one beat. Tap your foot evenly and count 1 2 3 4, a note on each count.",
     "N N N N"),
    ("half notes", "half",
     "A hollow note with a stem is a **half note**: two beats. Play it on 1, and let it ring through 2.",
     "N/2 N/2 N N N/2"),
    ("whole notes", "whole",
     "A hollow note with no stem is a **whole note**: the whole bar, all four beats.",
     "N/1 N/2 N/2"),
    ("quarter rests", "rest",
     "A **rest** is a silence you count. This squiggle is a quarter rest: one beat. Stop the string, and keep counting.",
     "N r N r N N/2 r"),
    ("eighth notes", "eighths",
     "Two **eighth notes** fit in one beat: count 1 and 2 and. Side by side they're joined by a beam; alone, an eighth has a flag.",
     "N/8 N/8 N/8 N/8 N N N/8 N/8 N/2 N"),
    ("dotted half notes", "dotted_half",
     "A **dot** after a note adds half its length again: a dotted half lasts three beats.",
     "N/2. N N/2. N"),
    ("half and whole rests", None,
     "A **half rest** sits on the middle line like a hat: two beats. A **whole rest** hangs under the fourth line: a whole bar.",
     "N N r/2 r/1"),
    ("eighth rests", "offbeat",
     "An **eighth rest** is half a beat of silence. After one, the note comes on the and: count 1 and, play on the and.",
     "N r/8 N/8 N r/8 N/8"),
    ("dotted quarter notes", "dotted_quarter",
     "A **dotted quarter** lasts a beat and a half: count 1 2 and. The eighth after it finishes beat 2.",
     "N/4. N/8 N/4. N/8 N/2 N/2"),
    ("sixteenth notes", "sixteenths",
     "Four **sixteenths** fit in a beat, joined by two beams: count 1 e and a.",
     "N/16 N/16 N/16 N/16 N/8 N/8 N/2"),
    ("triplets", "triplets",
     "Three notes in the time of two: a **triplet**, marked with a 3. Count 1 and a, 2 and a.",
     "N/8t N/8t N/8t N N/8t N/8t N/8t N"),
    ("dotted eighths", "dotted",
     "A **dotted eighth** and a **sixteenth** share a beat, long then short: a skip.",
     "N/8. N/16 N/8. N/16 N/2"),
]


class RhythmLadder:
    """Where a course is on the rhythm ladder: the rungs climbed, the cells learned"""

    def __init__(self):
        self.step = 0
        self.cells = []

    def rung(self, note):
        """The next rung's lines for a chapter's words (nothing once they're all climbed), on its note"""
        if self.step >= len(RHYTHM_LADDER):
            return []
        title, cell, text, music = RHYTHM_LADDER[self.step]
        self.step += 1
        if cell and cell not in self.cells:
            self.cells.append(cell)
        return [f"@heading Rhythm: {title}", f"text {text}", "@music " + music.replace("N", note)]

    def challenge(self, notes, extra, key=None):
        """A chapter's challenge, once there's more than one rhythm to read: its notes in every rhythm learned so far,
        to a beat, a little slower (optional: its stars count, the chapter's passed without it)"""
        if len(self.cells) < 2:
            return []
        return (["drill Challenge: in rhythm", "@optional", "type reading", "notes " + " ".join(notes), "cells " + " ".join(self.cells),
                 "bars 8", "tempo 60 90 5", "challenge 70", f"pass {BEAT_PASS}", "goal 1"] + key_lines(key) + extra)


RHYTHM = RhythmLadder()


def note_chapter(instrument, title, new, string_notes, known, key=None, intro=None):
    """A chapter bringing in `new`, with the other notes of its string and what's known so far"""
    lines = [f"lesson {title}"]
    if intro:
        lines.append(f"text {intro}")
    for note in new:
        lines.append(f"text {spoken(note)} sits {instrument.staff_text(note)}. On the {instrument.name} it's {instrument.place_text(note)}.")
    # Seen as well as said: written on the staff, and lit where it's played
    lines.append("@staff " + " ".join(new))
    lines.append("@neck " + " ".join(f"{instrument.place(note)[0] + 1}:{instrument.place(note)[1]}" for note in new))
    lines += RHYTHM.rung(new[0])
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
    # Last (so the drills before keep their numbers): the challenge, its notes in the rhythms learned so far
    extra = ["tuning " + " ".join(str(p) for p in BASS.tuning)] if instrument is BASS else []
    lines += RHYTHM.challenge(group, extra, key)
    return lines


def key_lines(key):
    """A drill's key signature lines, for a key like 'G major' (none for no key)"""
    if not key:
        return []
    tonic, mode = key.split()[:2]
    return [f"key {tonic}", f"scale {'major' if mode == 'major' else 'minor'}"]


def beat_drill(instrument, name, notes, where, key=None):
    """Notes at random, read to a metronome (where: the neck lighting each one): a little faster each clean pass"""
    lines = [f"drill {name}", "type reading", "notes " + " ".join(notes)]
    if where:
        lines.append("where yes")
    lines += ["cells quarter", f"bars {BEAT_BARS}", BEAT_TEMPO, BEAT_CHALLENGE, f"pass {BEAT_PASS}", "goal 1"]
    if key:
        tonic, mode = key.split()[:2]
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


# --- The piano's course: its own steps (the treble clef for the right hand, the bass clef for the left) ---------------

PIANO_TREBLE_TEXT = ("Music is written on five lines, the staff. The higher a note sits, the higher it sounds. The curl at the start is "
                     "the treble clef, for your right hand. Piano music is written just where it sounds.")
PIANO_BASS_TEXT = ("The bass clef, the curl with two dots, is for the lower notes, your left hand's. The same five lines, other names: "
                   "middle C is just above it, on its own short line.")
TREBLE = Instrument("piano", [0], [], "E5", "")  # (staff_text counts written an octave up, as guitar music is: not piano)
BASS_CLEF = Instrument("piano", [0], [], "G3", "")

KEY_PLACES = {
    "C": "the white key just left of a group of two black keys",
    "D": "the white key between the two black keys",
    "E": "the white key just right of the two black keys",
    "F": "the white key just left of a group of three black keys",
    "G": "the white key between the first two of the three black keys",
    "A": "the white key between the last two of the three black keys",
    "B": "the white key just right of the three black keys",
}


def which_one(name):
    """'E4' -> 'the E above middle C', 'C4' -> 'middle C', 'A3' -> 'the A below middle C'"""
    pitch = parse(name)[0]
    said = spoken(name)
    if pitch == 60:
        return "middle C"
    if pitch == 72:
        return "the C above middle C"
    if pitch == 48:
        return "the C below middle C"
    if 60 < pitch < 72:
        return f"the {said} above middle C"
    if 48 < pitch < 60:
        return f"the {said} below middle C"
    if pitch > 72:
        return f"the {said} above the C above middle C"
    return f"the low {said}, below the C below middle C"


def piano_title(name):
    """'E4' -> 'E above middle C', 'C4' -> 'Middle C'"""
    said = which_one(name)
    said = said[4:] if said.startswith("the ") else said
    return said[0].upper() + said[1:]


def key_text(name):
    """Where it is on the keyboard"""
    if len(name) > 1 and name[1] in "#b":
        neighbour = name[0]
        side = "right" if name[1] == "#" else "left"
        return f"the black key just {side} of {neighbour}: {which_one(name)}"
    return f"{KEY_PLACES[name[0]]}: {which_one(name)}"


def piano_drill(name, notes, where, key=None):
    lines = [f"drill {name}", "type reading", "instrument piano", "notes " + " ".join(notes)]
    if where:
        lines.append("where yes")
    lines += ["cells quarter", f"bars {BEAT_BARS}", BEAT_TEMPO, BEAT_CHALLENGE, f"pass {BEAT_PASS}", "goal 1"]
    if key:
        tonic, mode = key.split()[:2]
        lines += [f"key {tonic}", f"scale {'major' if mode == 'major' else 'minor'}"]
    return lines


def piano_note_chapter(clef, title, new, around, known, intro=None):
    """A chapter bringing in `new` (on one clef): alone, with its neighbours, on its own, mixed with that clef's notes"""
    lines = [f"lesson {title}"]
    if intro:
        lines.append(f"text {intro}")
    for note in new:
        lines.append(f"text {spoken(note)} sits {clef.staff_text(note)}. On the piano it's {key_text(note)}.")
    lines.append("@staff " + " ".join(new))
    lines.append("@keys " + " ".join(new))
    lines += RHYTHM.rung(new[0])
    group = list(dict.fromkeys(around + new))
    mixed = list(dict.fromkeys(known + new))
    if len(new) == 1:
        lines += piano_drill(f"{spoken(new[0])} alone", new, True)
    if len(group) > len(new):
        lines += piano_drill(f"{listing(group)}, shown where", group, True)
    lines += piano_drill(f"{listing(group)}, on your own", group, False)
    if len(mixed) > len(group):
        lines += piano_drill("Mixed with what you know", mixed[-9:], False)
    lines += RHYTHM.challenge(group, ["instrument piano"])
    return lines


def piano_review(title, notes, text, key=None):
    lines = [f"lesson {title}", f"text {text}"]
    lines += piano_drill("Shown where", notes, True, key)
    lines += piano_drill("On your own", notes, False, key)
    return lines


def piano_timed(title, text, low, high, cells, key="C major", leap=2):
    tonic, mode = key.split()
    scale = "major" if mode == "major" else "minor"
    lines = [f"lesson {title}", f"text {text}"]
    for label, start, goal in SLOW:
        lines += [f"drill {label}", "type reading", "instrument piano", f"key {tonic}", f"scale {scale}", f"range {low} {high}", f"leap {leap}",
                  "cells " + " ".join(cells), f"bars {BEAT_BARS}", f"tempo {start} {goal} 5", f"pass {BEAT_PASS}", "goal 1"]
    return lines


def build_piano():
    global RHYTHM
    RHYTHM = RhythmLadder()  # each course climbs it from the start
    out = ["# lahn course: written by tools/courses/make_reading_course.py (change it there, and run it again)",
           "version 1",
           "title Reading music",
           "description From middle C to reading both clefs to a beat, in every key: a note or two at a time.",
           "instrument piano", ""]

    def level(title):
        out.extend(["", f"unit {title}", ""])

    def chapter(lines):
        out.extend(lines + [""])

    def walk(clef, notes, known, intro):
        for i, name in enumerate(notes):
            before = known[-3:]
            chapter(piano_note_chapter(clef, piano_title(name), [name], before, known, intro=intro if i == 0 else None))
            known.append(name)

    treble, bass = [], []
    level("The treble clef: middle C and up")
    walk(TREBLE, ["C4", "D4", "E4", "F4", "G4"], treble, PIANO_TREBLE_TEXT)
    chapter(piano_review("Five fingers", ["C4", "D4", "E4", "F4", "G4"], "Your right hand's five fingers on C, D, E, F and G: read them as they come."))

    level("The treble clef: up to the top")
    walk(TREBLE, ["A4", "B4", "C5", "D5", "E5", "F5", "G5"], treble, None)
    chapter(piano_review("The whole treble staff", ["C4", "D4", "E4", "F4", "G4", "A4", "B4", "C5", "D5", "E5", "F5", "G5"],
                         "From middle C to the G above the staff."))

    level("The bass clef: middle C and down")
    walk(BASS_CLEF, ["C4", "B3", "A3", "G3", "F3", "E3", "D3", "C3"][1:], bass + ["C4"], PIANO_BASS_TEXT)
    bass = ["C4", "B3", "A3", "G3", "F3", "E3", "D3", "C3"]
    chapter(piano_review("Five fingers, left hand", ["C3", "D3", "E3", "F3", "G3"], "Your left hand's five fingers, little finger on the C below middle C."))

    level("The bass clef: down to the bottom")
    walk(BASS_CLEF, ["B2", "A2", "G2", "F2", "E2"], bass, None)
    chapter(piano_review("The whole bass staff", ["E2", "F2", "G2", "A2", "B2", "C3", "D3", "E3", "F3", "G3", "A3", "B3"],
                         "From the E below the staff up to B, just under middle C."))

    level("Sharps and flats")
    for new, around, clef, intro in [
        ("F#4", ["E4", "F4", "G4"], TREBLE, "A sharp sign raises a note to the very next key up, mostly a black one; a flat lowers it to the next key down."),
        ("C#4", ["C4", "D4"], TREBLE, None),
        ("Bb4", ["A4", "B4", "C5"], TREBLE, "Bb is B flat: the black key just left of B."),
        ("Eb4", ["D4", "E4"], TREBLE, None),
        ("G#4", ["G4", "A4"], TREBLE, None),
        ("F#3", ["E3", "F3", "G3"], BASS_CLEF, None),
        ("Bb2", ["A2", "B2", "C3"], BASS_CLEF, None),
    ]:
        chapter(piano_note_chapter(clef, piano_title(new), [new], around, [], intro=intro))

    level("Key signatures")
    for key, text, notes in [
        ("G major", "One sharp, F#: every F is played F sharp, though no sign stands by it.", ["D4", "E4", "F#4", "G4", "A4", "B4", "C5", "D5"]),
        ("F major", "One flat, Bb: every B is played B flat.", ["C4", "D4", "E4", "F4", "G4", "A4", "Bb4", "C5"]),
        ("D major", "Two sharps, F# and C#.", ["D4", "E4", "F#4", "G4", "A4", "B4", "C#5", "D5"]),
        ("G major, left hand", "The same one sharp, in the bass clef.", ["G2", "A2", "B2", "C3", "D3", "E3", "F#3", "G3"]),
        ("Bb major", "Two flats: Bb, Eb.", ["Bb3", "C4", "D4", "Eb4", "F4", "G4", "A4", "Bb4"]),
        ("A major", "Three sharps: F#, C#, G#.", ["A3", "B3", "C#4", "D4", "E4", "F#4", "G#4", "A4"]),
    ]:
        chapter(piano_review(f"In {key}", notes, text, key=key))

    level("Reading to a beat")
    for title, text, low, high, cells in [
        ("Five fingers, quarter notes", "Now melodies, a note on each beat: your right hand on C to G. A bar counts you in.", "C4", "G4", ["quarter"]),
        ("Rests", "A rest is a beat with no note: lift the finger, and count it.", "C4", "G4", ["quarter", "rest"]),
        ("Eighth notes", "Two notes on a beat: count 1 and 2 and.", "C4", "G4", ["quarter", "eighths"]),
        ("Up to the next C", "The hand moves: middle C to the C above.", "C4", "C5", ["quarter", "eighths", "rest"]),
        ("Left hand", "The same in the bass clef, your left hand on C to G.", "C3", "G3", ["quarter", "rest"]),
        ("Left hand, eighths", "Two on a beat, left hand.", "C3", "G3", ["quarter", "eighths"]),
        ("Off the beat", "An eighth rest, then a note: the and of the beat.", "C4", "G4", ["quarter", "offbeat", "rest"]),
        ("Dotted rhythms", "A long note and a short one: a dotted eighth and a sixteenth.", "C4", "G4", ["quarter", "dotted"]),
        ("Triplets", "Three notes on a beat.", "C4", "G4", ["quarter", "triplets"]),
        ("Sixteenths", "Four notes on a beat, slowly.", "C4", "G4", ["quarter", "sixteenths"]),
    ]:
        chapter(piano_timed(title, text, low, high, cells))

    level("Keys to a beat")
    for key, low, high in [("G major", "G4", "G5"), ("F major", "F4", "F5"), ("D major", "D4", "D5"), ("A minor", "A3", "A4"), ("E minor", "E4", "E5")]:
        chapter(piano_timed(f"{key}, to a beat", f"Melodies in {key}, read to a beat.", low, high, ["quarter", "eighths", "rest"], key=key))

    return "\n".join(out).rstrip() + "\n"


def build(instrument):
    global RHYTHM
    RHYTHM = RhythmLadder()  # each course climbs it from the start
    plan = PLANS[instrument.name]
    out = ["# lahn course: written by tools/courses/make_reading_course.py (change it there, and run it again)",
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
    for instrument, file in ((GUITAR, "02-reading.course"), (BASS, "02-reading-bass.course"), (None, "02-reading-piano.course")):
        path = os.path.join(here, "..", "..", "resources", "courses", file)
        text = to_v2(build(instrument) if instrument else build_piano())
        with open(path, "w") as f:
            f.write(text)
        drills = sum(1 for line in text.splitlines() if line.strip().startswith("block exercise"))
        chapters = sum(1 for line in text.splitlines() if line.startswith("chapter "))
        levels = sum(1 for line in text.splitlines() if line.startswith("level "))
        print(f"{os.path.normpath(path)}: {levels} levels, {chapters} chapters, {drills} drills")
