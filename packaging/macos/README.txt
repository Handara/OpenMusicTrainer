lahn for macOS
==============

A rhythm game you play on your real instrument: guitar or bass through an audio interface, or a MIDI keyboard.
This is an early build, made for trying it out.

The first time
--------------

lahn isn't signed by Apple, so macOS stops it until you say it's fine. Once is enough.

1. Unzip lahn-macos.zip (double-click it). You get a folder called "lahn".
2. Open Terminal (Applications > Utilities > Terminal), type the line below with a space at the end, drag the
   "lahn" folder onto the Terminal window (it writes the folder's place), and press Return:

       xattr -dr com.apple.quarantine

   This tells macOS that you trust the files in that folder.
3. Double-click "Start lahn.command". Terminal opens and starts lahn.
4. When macOS asks whether Terminal may use the microphone, allow it: that's how lahn hears your audio interface.
   If you said no, turn it on in System Settings > Privacy & Security > Microphone > Terminal, then start lahn again.

After that, double-click "Start lahn.command" whenever you want to play.

Your instrument
---------------

Plug your guitar or bass into your audio interface first. In lahn, open Settings and choose your interface as the
input, and which of its inputs your guitar or bass is on. Then try the Instrument screen from the main menu: the notes
you play light up on the neck.

Your songs, scores and settings are kept in your home folder, in Library/Application Support/lahn.

Not on macOS yet
----------------

- The video and stems add-ons (they exist for Windows and Linux only for now).
- File dialogs: to bring in a song or a tab, drag the file onto lahn's window.
