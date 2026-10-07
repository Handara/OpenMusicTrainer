# lahn

Music trainer and rhythm game for guitar, bass, piano and voice. It listens to your instrument and checks if you played the right note at the right time. *lahn* means melody in Arabic.

- Songs: notes on a highway, a score or tabs; any part on a piano keyboard (MIDI or the computer keys), or as taiko-style rhythm
- Scoring built for competing: timing judged to the millisecond, accuracy, grades, best runs per song
- An audio interface's inputs kept apart: each instrument on its own input; on Windows, ASIO drivers for the lowest latency
- Note detection from the mic: onsets, YIN pitch, hammer-ons, pull-offs and slides
- Drills: scales, fretboard, intervals, chords, rhythm, sight reading, singing
- Lessons: plain text files with images, audio, video and exercises, plus an editor
- Chart editor, songs shared as a single `.lahn` file
- Tuner, latency calibration, light/dark theme

## Build

CMake 3.26+, a C++17 compiler. CMake fetches the rest (raylib, Dear ImGui, miniaudio, pl_mpeg, miniz, doctest).

```
cmake -S . -B build
cmake --build build -j
./build/lahn
ctest --test-dir build --output-on-failure
```

## License

lahn is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License
as published by the Free Software Foundation, version 3 or (at your option) any later version. It is distributed
in the hope that it will be useful, but WITHOUT ANY WARRANTY; see [LICENSE](LICENSE) for the details.

Copyright (C) 2026 handara.

The name **lahn**, the wordmark **لحن** and the logo are not covered by the licence: forks are welcome, under a
name of their own. Contributions come with a contributor licence agreement, see [CONTRIBUTING.md](CONTRIBUTING.md).

The fonts are under the SIL Open Font License (their licences are next to them in `resources/fonts`): Figtree,
Chivo Mono, Reem Kufi (the wordmark) and Bravura (music symbols). The libraries lahn builds with keep their own
licences: raylib and rlImGui (zlib), Dear ImGui, pl_mpeg, miniz and doctest (MIT), miniaudio (public domain or
MIT-0). On Windows, lahn builds with Steinberg's ASIO SDK, under its GPLv3 option; ASIO is a trademark and software
of Steinberg Media Technologies GmbH.
