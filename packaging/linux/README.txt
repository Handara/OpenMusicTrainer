lahn for Linux
==============

A rhythm game you play on your real instrument: guitar or bass through an audio interface (or a mic), or a MIDI
keyboard. This is a very early build, made for trying it out.

Unpack it and start it from its folder:

    tar -xzf lahn-linux.tar.gz
    cd lahn
    ./lahn

It needs a desktop with OpenGL 3.3 (any graphics card or chip from the last ten years) and runs on systems from 2022
on (glibc 2.35: Ubuntu 22.04, Debian 12, Fedora 36 and newer). Sound goes through PipeWire, PulseAudio or ALSA, which
ever your system has.

Your instrument
---------------

Plug your guitar or bass into your audio interface first. In lahn, open Settings and choose your interface as the
input, and which of its inputs your guitar or bass is on. Then try the Instrument screen from the main menu: the notes
you play light up on the neck.

For the piano, plug in a MIDI keyboard and choose it in Settings (MIDI keyboard), or play on the computer's keys.

Your songs, scores and settings are kept in ~/.local/share/lahn.

To bring in a song or a tab, drag the file onto lahn's window, or use its Open buttons (they need zenity or kdialog).
