"""Writes a course as version 2 (levels of chapters, each chapter a lesson's pages: core/course.h), from the version 1
lines the course tools build: units, lessons, their text and their drills. Each drill keeps the number its step had
(its 'id'), so the progress kept by it stays where it is.

A tool may put pictures in a chapter's words with lines of its own, after the text:
  @staff E4 F4        a staff with these notes
  @neck 6:1 6:3       part of the neck with these places lit (string:fret, 1 = the lowest string)
  @keys D4            piano keys (that note's octave) with it lit
  @heading Rhythm     a heading among the words
  @music E4/2 E4 E4   a staff of this music, as staff blocks write it (each note its length: core/lessondoc.h)
And in a drill's settings:
  @optional           a challenge: its stars count, but the chapter's passed without it
"""

STRUCTURAL = {"unit", "lesson", "title", "text", "exercise", "drill", "goal"}


def _steps(lines):
    """A lesson's lines as its steps, the way core/course reads version 1: a title starts a text step, text goes on the
    text step just before (or starts one), a drill or an exercise is a step, and its settings follow it"""
    steps = []
    last_was_text = False
    for line in lines:
        key, _, rest = line.partition(" ")
        if key == "@optional":  # (the drill's own)
            steps[-1]["optional"] = True
            continue
        if key.startswith("@"):  # a picture: with the text before it
            if not steps or steps[-1]["kind"] != "text":
                steps.append({"kind": "text", "title": "", "items": []})
            steps[-1]["items"].append((key[1:], rest))
            last_was_text = True
            continue
        if key in ("title", "text"):
            if key == "title" or not last_was_text:
                steps.append({"kind": "text", "title": "", "items": []})
            if key == "title":
                steps[-1]["title"] = rest
            else:
                steps[-1]["items"].append(("text", rest))
            last_was_text = True
            continue
        last_was_text = False
        if key in ("drill", "exercise"):
            step = {"kind": "drill", "name": rest if key == "drill" else "", "named": rest if key == "exercise" else "",
                    "lines": [], "goal": ""}
            steps.append(step)
        elif key == "goal":
            steps[-1]["goal"] = rest
        else:
            steps[-1]["lines"].append(line)
    return steps


def _picture(kind, rest):
    """A picture's block lines"""
    if kind == "staff":
        # Lasting the bar between them, as a picture of notes is drawn (one a whole note, two halves), not quarters and rests
        notes = rest.split()
        length = {1: "/1", 2: "/2"}.get(len(notes), "")
        return ["  block staff", "    notes " + " ".join(note + length for note in notes)]
    if kind == "neck":
        frets = max([int(place.split(":")[1]) for place in rest.split()] + [3])
        return ["  block fretboard", f"    frets 0 {max(4, frets + 1)}", f"    lit {rest}"]
    if kind == "heading":
        return ["  block heading", f"    text {rest}"]
    if kind == "music":
        return ["  block staff", f"    notes {rest}"]
    if kind == "keys":
        octave = rest.split()[0][-1]
        return ["  block keyboard", f"    from C{octave}", f"    to B{octave}", f"    lit {rest}"]
    raise ValueError(f"unknown picture @{kind}")


def _chapter(steps, instrument):
    """A chapter's page: its words (and pictures) then its drills, each numbered by its step"""
    out = ["page"]
    for number, step in enumerate(steps, 1):
        if step["kind"] == "text":
            if step["title"]:
                out += ["  block heading", f"    text {step['title']}"]
            # In the order written: paragraphs side by side are one text block
            for i, (kind, rest) in enumerate(step["items"]):
                if kind != "text":
                    out += _picture(kind, rest)
                    continue
                if i == 0 or step["items"][i - 1][0] != "text":
                    out.append("  block text")
                out.append(f"    text {rest}")
            continue
        out.append("  block exercise" + (f" {step['name']}" if step["name"] else ""))
        out.append(f"    id {number}")
        if step.get("optional"):
            out.append("    gate no")
        if step["goal"]:
            out.append(f"    goal {step['goal']}")
        if step["named"]:
            out.append(f"    exercise {step['named']}")
        lines = list(step["lines"])
        # The course's instrument, unless the drill says, for the kinds played on one (as version 1 courses had it)
        text = "\n".join(lines)
        played_on_one = "type notes" in text or "type neck" in text or (instrument == "piano" and "type reading" in text)
        if lines and instrument != "guitar" and played_on_one and "instrument " not in text:
            lines.append(f"instrument {instrument}")
        out += [f"    {line}" for line in lines]
    return out


def to_v2(v1_text):
    """A version 1 course's text, as version 2"""
    header, chapters = [], []
    instrument = "guitar"
    current = None
    for raw in v1_text.splitlines():
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        key, _, rest = line.partition(" ")
        if key == "unit":
            chapters.append(("level", rest, None))
            current = None
        elif key == "lesson":
            current = [rest, []]
            chapters.append(("chapter", rest, current))
        elif current is not None:
            current[1].append(line)
        elif key == "version":
            continue
        else:
            if key == "instrument":
                instrument = rest
            header.append(line)
    out = ["# lahn course: written by the course tools (tools/courses; change it there, and run them again)", "version 2"] + header
    for kind, title, chapter in chapters:
        if kind == "level":
            out += ["", "", f"level {title}"]
        else:
            out += ["", f"chapter {title}"] + _chapter(_steps(chapter[1]), instrument)
    return "\n".join(out) + "\n"
