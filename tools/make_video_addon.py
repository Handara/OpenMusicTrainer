#!/usr/bin/env python3
"""Builds hardthz's video add-on: FFmpeg, the program that reads every kind of video, as one file to drop on hardthz.

    python3 tools/make_video_addon.py [windows|linux] [output folder]

hardthz plays one kind of video itself (MPEG-1, src/video). With this add-on, a video of any kind (mp4, mkv, webm,
mov...) is turned into that when it's brought into a song, and its sound into the song's audio (src/app/videoconvert.cpp).
It downloads, once, into a cache folder, a build of FFmpeg (GPL version 3):
  - Windows: gyan.dev's "essentials" build, github.com/GyanD/codexffmpeg
  - Linux: John Van Sickle's static build, johnvansickle.com/ffmpeg
and packs the ffmpeg program, its license, the credits and addon.txt into hardthz-video-<system>.hardthzaddon, about 40 MB.
hardthz itself is built without it, and runs it as a separate program.
"""
import os
import sys
import tarfile
import urllib.request
import zipfile

FFMPEG_VERSION = "9.0.2"  # the Windows build's; the Linux one is that site's current release
ADDON_VERSION = 1
BUILDS = {
    # system: (where it's downloaded from, the archive's name in the cache, the program's name inside it and in the add-on)
    "windows": (f"https://github.com/GyanD/codexffmpeg/releases/download/{FFMPEG_VERSION}/ffmpeg-{FFMPEG_VERSION}-essentials_build.zip",
                f"ffmpeg-{FFMPEG_VERSION}-essentials_build.zip", "ffmpeg.exe"),
    "linux": ("https://johnvansickle.com/ffmpeg/releases/ffmpeg-release-amd64-static.tar.xz",
              "ffmpeg-release-amd64-static.tar.xz", "ffmpeg"),
}

CREDITS = """hardthz's video add-on

FFmpeg, by the FFmpeg developers: https://ffmpeg.org
This build is licensed under the GNU General Public License, version 3 (see LICENSE.txt): you may use it, share it
and change it under that license's terms. Its source code, and that of the libraries built into it:
  - the Windows build: https://www.gyan.dev/ffmpeg/builds/ and https://github.com/GyanD/codexffmpeg
  - the Linux build: https://johnvansickle.com/ffmpeg/
  - FFmpeg itself: https://ffmpeg.org/download.html
The program is used as published, unchanged. hardthz runs it as a separate program.
"""


def fetch(url, path):
    if os.path.exists(path):
        return
    print("downloading", url)
    request = urllib.request.Request(url, headers={"User-Agent": "hardthz-tools"})
    with urllib.request.urlopen(request) as response, open(path + ".part", "wb") as out:
        while True:
            block = response.read(1 << 20)
            if not block:
                break
            out.write(block)
    os.replace(path + ".part", path)


def files_from(archive, program):
    """The program's bytes and its license's, out of the build's archive (a zip for Windows, a tar for Linux)"""
    found = {}
    if archive.endswith(".zip"):
        with zipfile.ZipFile(archive) as z:
            for item in z.namelist():
                name = item.rsplit("/", 1)[-1]
                if name == program or name in ("LICENSE", "GPLv3.txt"):
                    found[name] = z.read(item)
    else:
        with tarfile.open(archive) as t:
            for item in t.getmembers():
                name = item.name.rsplit("/", 1)[-1]
                if item.isfile() and (name == program or name in ("LICENSE", "GPLv3.txt")):
                    found[name] = t.extractfile(item).read()
    if program not in found:
        raise SystemExit(f"{program} isn't in {archive}")
    license_text = found.get("GPLv3.txt") or found.get("LICENSE") or b"GNU General Public License, version 3: https://www.gnu.org/licenses/gpl-3.0.txt\n"
    return found[program], license_text


def main():
    system = sys.argv[1] if len(sys.argv) > 1 else ("windows" if os.name == "nt" else "linux")
    out_dir = sys.argv[2] if len(sys.argv) > 2 else "."
    if system not in BUILDS:
        raise SystemExit("windows or linux")
    cache = os.path.join(os.path.expanduser("~"), ".cache", "hardthz-video")
    os.makedirs(cache, exist_ok=True)
    os.makedirs(out_dir, exist_ok=True)
    url, archive, program = BUILDS[system]
    fetch(url, os.path.join(cache, archive))
    binary, license_text = files_from(os.path.join(cache, archive), program)
    out = os.path.join(out_dir, f"hardthz-video-{system}.hardthzaddon")
    with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as addon:
        addon.writestr("addon.txt", f"hardthz_addon 1\nname video\nversion {ADDON_VERSION}\n")
        addon.writestr("CREDITS.txt", CREDITS)
        addon.writestr("LICENSE.txt", license_text)
        info = zipfile.ZipInfo(program)
        info.external_attr = 0o755 << 16  # it's a program: kept runnable where that's recorded
        info.compress_type = zipfile.ZIP_DEFLATED
        addon.writestr(info, binary)
    print(out, f"{os.path.getsize(out) / 1e6:.0f} MB")


if __name__ == "__main__":
    main()
