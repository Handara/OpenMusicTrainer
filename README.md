# lahn

Music trainer for guitar, bass and voice. It listens through the mic and checks if you played the right note at the right time. *lahn* means melody in Arabic.

- Songs: notes on a highway, a score or tabs
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
