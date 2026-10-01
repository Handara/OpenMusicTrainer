#!/usr/bin/env python3
"""Builds lahn's stems add-on: the bass model and the runtime that runs it, as one file to drop on lahn.

    python3 tools/make_stems_addon.py [windows|linux] [output folder]

It downloads, once, into a cache folder:
  - ONNX Runtime (MIT), the library that runs the model: github.com/microsoft/onnxruntime
  - KUIELab-MDX-Net's models (code MIT, weights CC-BY 4.0): zenodo.org/record/5717356
and packs the runtime's library, the bass model, the credits those licenses ask for and addon.txt into
lahn-stems-<system>.lahnaddon, about 40 MB. lahn itself is built without either (src/app/stemmodel.cpp).
"""
import io
import os
import sys
import tarfile
import urllib.request
import zipfile

ORT_VERSION = "1.30.0"  # the headers in third_party/onnxruntime are this version's
ADDON_VERSION = 1
RUNTIMES = {
    # system: (archive, the library's name inside it, the name lahn loads)
    "windows": (f"onnxruntime-win-x64-{ORT_VERSION}.zip", "onnxruntime.dll", "onnxruntime.dll"),
    "linux": (f"onnxruntime-linux-x64-{ORT_VERSION}.tgz", f"libonnxruntime.so.{ORT_VERSION}", "libonnxruntime.so"),
}
ORT_URL = "https://github.com/microsoft/onnxruntime/releases/download/v" + ORT_VERSION + "/"
MODELS_URL = "https://zenodo.org/records/5717356/files/onnx_A.zip?download=1"

CREDITS = f"""lahn's stems add-on

The bass model: KUIELab-MDX-Net, by Minseok Kim, Woosung Choi, Jaehwa Chung, Daewon Lee and Soonyoung Jung
("KUIELab-MDX-Net: A Two-Stream Neural Network for Music Demixing", 2021).
Code under the MIT license, weights under Creative Commons Attribution 4.0 (CC-BY 4.0):
https://github.com/kuielab/mdx-net-submission and https://zenodo.org/record/5717356
The model is used as published, unchanged.

The runtime: ONNX Runtime {ORT_VERSION}, by Microsoft, under the MIT license:
https://github.com/microsoft/onnxruntime
"""


def fetch(url, path):
    if os.path.exists(path):
        return
    print("downloading", url)
    with urllib.request.urlopen(url) as response, open(path + ".part", "wb") as out:
        while True:
            block = response.read(1 << 20)
            if not block:
                break
            out.write(block)
    os.replace(path + ".part", path)


def library_from(archive, name):
    """The library's bytes, out of the runtime's archive (a zip for Windows, a tar for Linux)"""
    if archive.endswith(".zip"):
        with zipfile.ZipFile(archive) as z:
            for item in z.namelist():
                if item.endswith("/lib/" + name):
                    return z.read(item)
    else:
        with tarfile.open(archive) as t:
            for item in t.getmembers():
                if item.name.endswith("/lib/" + name) and item.isfile():
                    return t.extractfile(item).read()
    raise SystemExit(f"{name} isn't in {archive}")


def main():
    system = sys.argv[1] if len(sys.argv) > 1 else ("windows" if os.name == "nt" else "linux")
    out_dir = sys.argv[2] if len(sys.argv) > 2 else "."
    if system not in RUNTIMES:
        raise SystemExit("windows or linux")
    cache = os.path.join(os.path.expanduser("~"), ".cache", "lahn-stems")
    os.makedirs(cache, exist_ok=True)
    os.makedirs(out_dir, exist_ok=True)
    archive, inside, loaded = RUNTIMES[system]
    fetch(ORT_URL + archive, os.path.join(cache, archive))
    fetch(MODELS_URL, os.path.join(cache, "onnx_A.zip"))
    library = library_from(os.path.join(cache, archive), inside)
    with zipfile.ZipFile(os.path.join(cache, "onnx_A.zip")) as models:
        bass = models.read("onnx_A/bass.onnx")
    out = os.path.join(out_dir, f"lahn-stems-{system}.lahnaddon")
    with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as addon:
        addon.writestr("addon.txt", f"lahn_addon 1\nname stems\nversion {ADDON_VERSION}\n")
        addon.writestr("CREDITS.txt", CREDITS)
        addon.writestr(loaded, library)
        addon.writestr("bass.onnx", bass)
    print(out, f"{os.path.getsize(out) / 1e6:.0f} MB")


if __name__ == "__main__":
    main()
