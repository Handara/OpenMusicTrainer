**A very early pre-alpha of lahn**, a free and open-source music trainer and rhythm game that you play on your real
instrument: guitar or bass through an audio interface (or a mic), piano on a MIDI keyboard or the computer's keys.
It's here to be tried and broken. Expect rough edges.

## Download

| System | File | To start it |
|---|---|---|
| Windows 10 and 11 | `lahn-windows.zip` | Unzip, run `lahn.exe`. It isn't signed yet: on "Windows protected your PC", click *More info*, then *Run anyway*. |
| macOS 13.3 and later (Apple Silicon and Intel) | `lahn-macos.zip` | Unzip, then follow `README.txt` inside (one Terminal line lets the unsigned app run). |
| Linux (glibc 2.35+: Ubuntu 22.04, Debian 12, Fedora 36 and newer) | `lahn-linux.tar.gz` | `tar -xzf lahn-linux.tar.gz && cd lahn && ./lahn` |

Each download has a `README.txt` for connecting your instrument.

## Feedback

What helps most right now: **which audio interface, mic or MIDI keyboard you used, and whether lahn heard your notes
correctly and on time.** Please [open an issue](https://github.com/Handara/OpenMusicTrainer/issues) with your system, your interface and your instrument.
Crashes write `crash.txt` into lahn's data folder (each read-me says where it is): attach it if you have one.
